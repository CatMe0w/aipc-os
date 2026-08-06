from __future__ import annotations

import struct
from pathlib import Path

PAGE_DATA = 2048
PAGE_SPARE = 64
PAGE_RAW = PAGE_DATA + PAGE_SPARE

# Pages per block is deliberately absent. 64 is the known lower bound and is
# safe for all known devices.
BLOCK_PAGES = 64

# Always use 1.88 DDR init sequence. It is safe on devices with older firmware.
FIRMWARE = "1.88"

PAYLOAD = Path(__file__).resolve().parent.parent / "opennboot.bin"

STUB_DIR = Path(__file__).resolve().parent / "stub"
READ_BIN = STUB_DIR / "nf_read.bin"
WRITE_BIN = STUB_DIR / "nf_write.bin"

# Must match tools/stub/Makefile and nf_common.h.
READ_BASE, READ_CMD = 0x30110000, 0x30113000
WRITE_BASE, WRITE_CMD = 0x30114000, 0x30117000
BUF = 0x30120000

# A 128-page erase plus program runs well under a second. This is only a bound.
EXEC_TIMEOUT = 15.0

ERRORS = {
    0: "ok",
    1: "sequencer never reported done",
    2: "L2 buffer never filled",
    3: "DMA never reported done",
    4: "the chip reported a failure status",
    5: "stub rejected its arguments",
}


class NandError(RuntimeError):
    pass


def after_write(image: bytes) -> bytes:
    out = bytearray(image)
    for off in range(PAGE_DATA, len(out), PAGE_RAW):
        out[off:off + PAGE_SPARE] = b"\xff" * PAGE_SPARE
    return bytes(out)


def describe_mismatch(got: bytes, want: bytes) -> str | None:
    if len(got) != len(want):
        return f"it is {len(got)} bytes, expected {len(want)}"
    for i, (a, b) in enumerate(zip(got, want)):
        if a != b:
            page, col = divmod(i, PAGE_RAW)
            return f"page {page} column {col:#06x} is {a:#04x}, expected {b:#04x}"
    return None


def confirm(prompt: str, word: str = "yes") -> bool:
    try:
        return input(f"{prompt} To proceed, type {word} and press Enter: ").strip() == word
    except (EOFError, KeyboardInterrupt):
        print()
        return False


class NandBlock:
    def __init__(self, dev) -> None:
        self.dev = dev
        self.last_id = 0
        for addr, path in ((READ_BASE, READ_BIN), (WRITE_BASE, WRITE_BIN)):
            if not path.exists():
                raise NandError(f"openNBOOT not found, build it first\n")
            dev.write_mem(addr, path.read_bytes())

    def read_pages(self, row: int, count: int = BLOCK_PAGES) -> bytes:
        self.dev.write_mem(READ_CMD, struct.pack("<4I", row, count, 0xDEAD, 0))
        self.dev.execute(READ_BASE, wait=True, timeout=EXEC_TIMEOUT)
        rc, fail, self.last_id = struct.unpack(
            "<3I", self.dev.read_mem(READ_CMD + 8, 12))
        if rc:
            raise NandError(
                f"Read of {count} pages from {row} failed at page {fail}: "
                f"{ERRORS.get(rc, rc)}"
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
        rc, fail, status = struct.unpack("<3I", self.dev.read_mem(WRITE_CMD + 8, 12))
        if rc:
            where = "erase" if fail >= count else f"page {row + fail}"
            wp = "" if status & 0x80 else "; part reports write protect"
            raise NandError(
                f"Write at row {row} failed at {where}: {ERRORS.get(rc, rc)} "
                f"(chip status {status:#04x}){wp}"
            )
        return count

    def check_ddr(self) -> None:
        probe = bytes(range(256)) * 8
        self.dev.write_mem(BUF, probe)
        if self.dev.read_mem(BUF, len(probe)) != probe:
            raise NandError(
                f"Memory did not come up: the buffer at {BUF:#010x} does not "
                f"read back what was written."
            )
