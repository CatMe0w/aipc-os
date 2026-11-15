from __future__ import annotations

import struct

from lz4.frame import LZ4FrameCompressor

from .images import resolve

PAGE_DATA = 2048
PAGE_SPARE = 64
PAGE_RAW = PAGE_DATA + PAGE_SPARE

BLOCK_PAGES = 64
PAYLOAD_MAX = (BLOCK_PAGES - 1) * PAGE_DATA

# Always the 1.88 DDR init sequence. It is safe on devices with older firmware.
FIRMWARE = "1.88"

# Must match stub/Makefile and stub/stub.h.
BLOCK0_BASE, BLOCK0_CMD = 0x30110000, 0x30113000
B0_CURRENT, B0_IMAGE, B0_PAYLOAD = 0x31000000, 0x31040000, 0x310C0000
BACKUP_BASE, BACKUP_CMD = 0x30118000, 0x3011B000
BACKUP_BUF = 0x31000000
BACKUP_MAX_PAGES = 4096
RESTORE_BASE, RESTORE_CMD = 0x3011C000, 0x3011F000

EXEC_TIMEOUT = 15.0

# Negative codes come from baremetal/lib/nand.h. The stubs add the positive one.
ERRORS = {
    0: "ok",
    1: "invalid stub arguments",
    -1: "NAND sequencer timeout (command)",
    -2: "NAND sequencer timeout (data)",
    -3: "L2 buffer transfer timeout",
    -4: "DMA timeout",
    -9: "invalid length",
    -11: "the NAND reported a failure status",
    -12: "invalid arguments",
    0xDEAD: "the stub did not finish",
}

HEADER_ERRORS = {
    -20: "the ANYKA382 signature is missing from the header page. The boot ROM "
         "does not start an image without it",
    -21: f"the header declares a page size other than {PAGE_DATA} bytes",
    -22: f"the header declares a bootloader of more than {BLOCK_PAGES - 1} pages",
}
PAYLOAD_TOO_LARGE = -23


class NandError(RuntimeError):
    pass


class WriteFailed(NandError):
    pass


def connect():
    from ak7802_usbboot.transport import find_device
    from aipc_ddr_init.cli import ddr_init

    dev = find_device(wait=True)
    ddr_init(dev, firmware=FIRMWARE, stub=resolve("ddr_init_v1_88.bin"))
    return dev


def id_hex(nand_id: int) -> str:
    return nand_id.to_bytes(4, "little").hex()


def xxh32(data: bytes) -> int:
    lz4 = LZ4FrameCompressor(content_checksum=True)
    frame = lz4.begin() + lz4.compress(data) + lz4.flush()
    return int.from_bytes(frame[-4:], "little")


def check_buffer(dev, addr: int) -> None:
    probe = bytes(range(256)) * 8
    dev.write_mem(addr, probe)
    if dev.read_mem(addr, len(probe)) != probe:
        raise NandError(
            f"DDR initialization failed: memory test at {addr:#010x} did not "
            f"read back the written pattern"
        )


def confirm(prompt: str, word: str = "yes") -> bool:
    try:
        return input(f"{prompt}\nType '{word}' to proceed: ").strip() == word
    except (EOFError, KeyboardInterrupt):
        print()
        return False


# Every check of an image runs on the device, in baremetal/lib/block0.c.
class Block0:
    READ, BUILD, CHECK, WRITE = range(4)
    CHECK_STEP, ERASE_STEP, PROGRAM_STEP, READBACK_STEP, COMPARE_STEP = range(5)

    def __init__(self, dev) -> None:
        self.dev = dev
        dev.write_mem(BLOCK0_BASE, resolve("nf_block0.bin").read_bytes())
        check_buffer(dev, B0_CURRENT)

    def _run(self, mode: int, row: int = 0, length: int = 0):
        self.dev.write_mem(BLOCK0_CMD, struct.pack("<4I", mode, row, length, 0xDEAD))
        self.dev.execute(BLOCK0_BASE, wait=True, timeout=EXEC_TIMEOUT)
        rc, nand_id, *result = struct.unpack(
            "<i7I", self.dev.read_mem(BLOCK0_CMD + 12, 32))
        return rc, nand_id, result

    def read(self, row: int) -> tuple[bytes, int]:
        rc, nand_id, result = self._run(self.READ, row)
        if rc:
            raise NandError(
                f"Read failed at page {row + result[2]}: {ERRORS.get(rc, rc)}"
            )
        return self.dev.read_mem(B0_CURRENT, BLOCK_PAGES * PAGE_RAW), nand_id

    def build(self, payload: bytes) -> None:
        self.dev.write_mem(B0_PAYLOAD, payload)
        rc, _, _ = self._run(self.BUILD, length=len(payload))
        if rc == PAYLOAD_TOO_LARGE:
            raise NandError(
                f"Payload too large: {len(payload)} bytes, and one block "
                f"holds at most {PAYLOAD_MAX} bytes"
            )
        if rc:
            raise NandError(
                f"Unsupported contents of NAND block 0: "
                f"{HEADER_ERRORS.get(rc) or ERRORS.get(rc, rc)}. "
                f"The NAND was not modified."
            )

    def check(self, image: bytes, name: str) -> None:
        self.dev.write_mem(B0_IMAGE, image)
        rc, _, _ = self._run(self.CHECK)
        if rc:
            raise NandError(
                f"{name}: not a valid image of NAND block 0: "
                f"{HEADER_ERRORS.get(rc) or ERRORS.get(rc, rc)}. "
                f"The NAND was not modified."
            )

    def write(self, row: int) -> None:
        rc, _, (step, status, page, column, got, want) = self._run(self.WRITE, row)
        if rc == 0:
            return
        reason = HEADER_ERRORS.get(rc) or ERRORS.get(rc, rc)
        if step == self.CHECK_STEP:
            raise NandError(
                f"Invalid image: {reason}. The NAND was not modified.")
        if step == self.COMPARE_STEP:
            raise WriteFailed(
                f"Verification failed after the write: page {row + page}, "
                f"column {column:#06x}: read {got:#04x}, expected {want:#04x}"
            )
        if step == self.READBACK_STEP:
            raise WriteFailed(
                f"Read failed at page {row + page} after the write: {reason}"
            )
        where = "block erase" if step == self.ERASE_STEP else f"page {row + page}"
        wp = "" if status & 0x80 else ". The device reports write protection"
        raise WriteFailed(
            f"Write failed ({where}): {reason} (chip status {status:#04x}){wp}"
        )


class NandReader:
    def __init__(self, dev) -> None:
        self.dev = dev
        self.nand_id = self.pages = self.ppb = 0
        dev.write_mem(BACKUP_BASE, resolve("nf_backup.bin").read_bytes())
        check_buffer(dev, BACKUP_BUF)

    def read_pages(self, row: int, count: int, progress=None) -> bytes:
        self.dev.write_mem(BACKUP_CMD, struct.pack("<3I", row, count, 0xDEAD))
        self.dev.execute(BACKUP_BASE, wait=True, timeout=EXEC_TIMEOUT + count * 0.02)
        rc, fail, self.nand_id, self.pages, self.ppb = struct.unpack(
            "<i4I", self.dev.read_mem(BACKUP_CMD + 8, 20))
        if rc:
            raise NandError(
                f"Read failed at page {row + fail}: {ERRORS.get(rc, rc)}"
            )
        return self.dev.read_mem(BACKUP_BUF, count * PAGE_RAW, progress=progress)

    def identify(self) -> tuple[int, int, int]:
        self.read_pages(0, 1)
        if not self.pages:
            raise NandError(
                f"Unsupported NAND (ID {id_hex(self.nand_id)}): only devices "
                f"with {PAGE_DATA}-byte pages and a capacity of 256 MB to 1 GB "
                f"are supported"
            )
        return self.nand_id, self.pages, self.ppb


class NandWriter:
    WRITE, SCAN, ERASE = 0, 1, 2

    def __init__(self, dev, pages_per_block: int) -> None:
        self.dev = dev
        self.ppb = pages_per_block
        dev.write_mem(RESTORE_BASE, resolve("nf_restore.bin").read_bytes())

    def run(self, mode: int, row: int, count: int, image: bytes = b"",
            progress=None) -> list[int]:
        if mode == self.WRITE:
            self.dev.write_mem(BACKUP_BUF, image, progress=progress)
        self.dev.write_mem(
            RESTORE_CMD, struct.pack("<5I", row, count, self.ppb, mode, 0xDEAD))
        self.dev.execute(RESTORE_BASE, wait=True, timeout=EXEC_TIMEOUT + count * 0.02)
        rc, fail, status, bad = struct.unpack(
            "<i2IQ", self.dev.read_mem(RESTORE_CMD + 16, 20))
        if rc:
            wp = "" if status & 0x80 else "; the device reports write protection"
            raise NandError(
                f"Write failed at page {row + fail}: {ERRORS.get(rc, rc)} "
                f"(chip status {status:#04x}){wp}"
            )
        first = row // self.ppb
        return [first + i for i in range(count // self.ppb) if bad >> i & 1]
