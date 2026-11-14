from __future__ import annotations

import struct

from .images import resolve

PAGE_DATA = 2048
PAGE_SPARE = 64
PAGE_RAW = PAGE_DATA + PAGE_SPARE

# Nothing here probes the part for its real pages per block. 64 is the known
# lower bound, and it is safe on every device we have.
BLOCK_PAGES = 64

# Always the 1.88 DDR init sequence. It is safe on devices with older firmware.
FIRMWARE = "1.88"

# Must match stub/Makefile and stub/stub.h.
READ_BASE, READ_CMD = 0x30110000, 0x30113000
WRITE_BASE, WRITE_CMD = 0x30114000, 0x30117000
BUF = 0x30120000
BACKUP_BASE, BACKUP_CMD = 0x30118000, 0x3011B000
BACKUP_BUF = 0x31000000
BACKUP_MAX_PAGES = 4096
RESTORE_BASE, RESTORE_CMD = 0x3011C000, 0x3011F000

# READ ID byte 1 -> capacity in MB. The stubs issue five address cycles, so
# parts of 128 MB and below are not in this table.
CAPACITY_MB = {0xDA: 256, 0xAA: 256, 0xDC: 512, 0xAC: 512, 0xD3: 1024, 0xA3: 1024}

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


class NandError(RuntimeError):
    pass


def connect():
    from ak7802_usbboot.transport import find_device
    from aipc_ddr_init.cli import ddr_init

    dev = find_device(wait=True)
    ddr_init(dev, firmware=FIRMWARE, stub=resolve("ddr_init_v1_88.bin"))
    return dev


# Returns (pages, pages_per_block).
def geometry(nand_id: int) -> tuple[int, int]:
    device, fourth = (nand_id >> 8) & 0xFF, (nand_id >> 24) & 0xFF
    page = 1024 << (fourth & 3)
    block = 65536 << ((fourth >> 4) & 3)
    if device not in CAPACITY_MB or page != PAGE_DATA:
        raise NandError(
            f"Unsupported NAND (ID {nand_id.to_bytes(4, 'little').hex()}): only "
            f"devices with {PAGE_DATA}-byte pages and a capacity of 256 MB to "
            f"1 GB are supported"
        )
    return CAPACITY_MB[device] * 1024 * 1024 // page, block // page


def check_buffer(dev, addr: int) -> None:
    probe = bytes(range(256)) * 8
    dev.write_mem(addr, probe)
    if dev.read_mem(addr, len(probe)) != probe:
        raise NandError(
            f"DDR initialization failed: memory test at {addr:#010x} did not "
            f"read back the written pattern"
        )


def after_write(image: bytes) -> bytes:
    out = bytearray(image)
    for off in range(PAGE_DATA, len(out), PAGE_RAW):
        out[off:off + PAGE_SPARE] = b"\xff" * PAGE_SPARE
    return bytes(out)


def describe_mismatch(got: bytes, want: bytes) -> str | None:
    if len(got) != len(want):
        return f"size is {len(got)} bytes, expected {len(want)}"
    for i, (a, b) in enumerate(zip(got, want)):
        if a != b:
            page, col = divmod(i, PAGE_RAW)
            return f"page {page}, column {col:#06x}: read {a:#04x}, expected {b:#04x}"
    return None


def confirm(prompt: str, word: str = "yes") -> bool:
    try:
        return input(f"{prompt}\nType '{word}' to proceed: ").strip() == word
    except (EOFError, KeyboardInterrupt):
        print()
        return False


class NandBlock:
    def __init__(self, dev) -> None:
        self.dev = dev
        self.last_id = 0
        for addr, name in ((READ_BASE, "nf_read.bin"), (WRITE_BASE, "nf_write.bin")):
            dev.write_mem(addr, resolve(name).read_bytes())

    def read_pages(self, row: int, count: int = BLOCK_PAGES) -> bytes:
        self.dev.write_mem(READ_CMD, struct.pack("<4I", row, count, 0xDEAD, 0))
        self.dev.execute(READ_BASE, wait=True, timeout=EXEC_TIMEOUT)
        rc, fail, self.last_id = struct.unpack(
            "<i2I", self.dev.read_mem(READ_CMD + 8, 12))
        if rc:
            raise NandError(
                f"Read failed at page {row + fail}: {ERRORS.get(rc, rc)}"
            )
        return self.dev.read_mem(BUF, count * PAGE_RAW)

    def write_pages(self, row: int, image: bytes) -> int:
        if len(image) != BLOCK_PAGES * PAGE_RAW:
            raise ValueError(
                f"Image must be {BLOCK_PAGES * PAGE_RAW} bytes, got {len(image)}")

        count = 0
        for p in range(BLOCK_PAGES):
            off = p * PAGE_RAW
            if image[off:off + PAGE_DATA] != b"\xff" * PAGE_DATA:
                count = p + 1

        if count:
            self.dev.write_mem(BUF, image[:count * PAGE_RAW])
        self.dev.write_mem(WRITE_CMD, struct.pack("<2I", row, count))
        self.dev.execute(WRITE_BASE, wait=True, timeout=EXEC_TIMEOUT)
        rc, fail, status = struct.unpack("<i2I", self.dev.read_mem(WRITE_CMD + 8, 12))
        if rc:
            where = "block erase" if fail >= count else f"page {row + fail}"
            wp = "" if status & 0x80 else "; the device reports write protection"
            raise NandError(
                f"Write failed ({where}): {ERRORS.get(rc, rc)} "
                f"(chip status {status:#04x}){wp}"
            )
        return count

    def check_ddr(self) -> None:
        check_buffer(self.dev, BUF)


class NandReader:
    def __init__(self, dev) -> None:
        self.dev = dev
        dev.write_mem(BACKUP_BASE, resolve("nf_backup.bin").read_bytes())
        check_buffer(dev, BACKUP_BUF)

    def read_pages(self, row: int, count: int, progress=None) -> tuple[bytes, int]:
        self.dev.write_mem(BACKUP_CMD, struct.pack("<3I", row, count, 0xDEAD))
        self.dev.execute(BACKUP_BASE, wait=True, timeout=EXEC_TIMEOUT + count * 0.02)
        rc, fail, nand_id = struct.unpack(
            "<i2I", self.dev.read_mem(BACKUP_CMD + 8, 12))
        if rc:
            raise NandError(
                f"Read failed at page {row + fail}: {ERRORS.get(rc, rc)}"
            )
        return self.dev.read_mem(BACKUP_BUF, count * PAGE_RAW, progress=progress), nand_id


class NandWriter:
    WRITE, SCAN, ERASE = 0, 1, 2

    def __init__(self, dev, pages_per_block: int) -> None:
        self.dev = dev
        self.ppb = pages_per_block
        dev.write_mem(RESTORE_BASE, resolve("nf_restore.bin").read_bytes())

    # Returns the bad blocks of the run. The stub does not touch them.
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
