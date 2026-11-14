from __future__ import annotations

import sys
from datetime import datetime
from pathlib import Path

from .images import resolve
from .nand import (
    BLOCK_PAGES, PAGE_DATA, PAGE_RAW,
    NandBlock, NandError, after_write, confirm, connect, describe_mismatch,
)

SIGNATURE = b"ANYKA382"
SIG_OFFSET = 0x04
# The counts word at +0x0C holds chunks_per_page, then page_count.
# See docs/nboot/boot-flow.md.
CHUNKS_OFFSET = 0x0C
PAGE_COUNT_OFFSET = 0x0D

# The header page takes one page of the block.
MAX_PAYLOAD_PAGES = BLOCK_PAGES - 1


def build_image(original: bytes, payload: bytes) -> tuple[bytes, int]:
    header = original[:PAGE_DATA]
    if header[SIG_OFFSET:SIG_OFFSET + 8] != SIGNATURE:
        raise NandError(
            "ANYKA382 signature not found in the header page. The boot ROM "
            "would reject the resulting image. The NAND was not modified."
        )

    declared = header[CHUNKS_OFFSET] * 512
    if declared != PAGE_DATA:
        raise NandError(
            f"Unsupported page size in the header: {declared} bytes "
            f"(expected {PAGE_DATA})"
        )

    installed = header[PAGE_COUNT_OFFSET]
    if installed > MAX_PAYLOAD_PAGES:
        raise NandError(
            f"The installed bootloader occupies {installed} pages, which exceeds "
            f"the {MAX_PAYLOAD_PAGES} pages that can be backed up. Aborting."
        )

    page_count = (len(payload) + PAGE_DATA - 1) // PAGE_DATA
    if page_count > MAX_PAYLOAD_PAGES:
        raise NandError(
            f"Payload too large: {page_count} pages required, "
            f"{MAX_PAYLOAD_PAGES} available in one block"
        )

    patched = bytearray(header)
    patched[PAGE_COUNT_OFFSET] = page_count

    image = bytearray(b"\xff" * (BLOCK_PAGES * PAGE_RAW))
    image[0:PAGE_DATA] = patched
    for i in range(page_count):
        off = (i + 1) * PAGE_RAW
        chunk = payload[i * PAGE_DATA:(i + 1) * PAGE_DATA]
        image[off:off + PAGE_DATA] = chunk.ljust(PAGE_DATA, b"\xff")
    return bytes(image), page_count


def save_backup(original: bytes, path: Path) -> None:
    if path.exists():
        raise NandError(f"{path}: file exists. Refusing to overwrite.")
    path.write_bytes(original)
    problem = describe_mismatch(path.read_bytes(), original)
    if problem:
        raise NandError(
            f"{path}: verification of the saved backup failed: {problem}"
        )


def run(row: int = 0) -> int:
    # With a piped stdout, the "do not disconnect" line would otherwise appear
    # only after the write.
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    source = resolve("opennboot.bin")
    payload = source.read_bytes()
    backup = Path.cwd() / f"backup_nboot-{datetime.now():%Y%m%d-%H%M%S}.bin"

    print("Installing openNBOOT. The current bootloader will be backed up first.")
    print(f"Image: {source} ({len(payload)} bytes)")
    if row:
        print(f"Scratch target: row {row}.")

    dev = connect()
    print("Device connected.")

    nb = NandBlock(dev)
    nb.check_ddr()

    original = nb.read_pages(row)
    save_backup(original, backup)
    print(f"Current bootloader saved to {backup.name} ({len(original)} bytes).")
    print("Important: keep this file. It is the only copy of the original "
          "bootloader of this device.")
    print(f"To revert the installation: opennboot restore --image {backup.name}")

    image, _ = build_image(original, payload)

    if not confirm("The bootloader in NAND block 0 will be erased and replaced."):
        print("Aborted. The NAND was not modified.")
        return 0

    print("Writing to NAND. Do not disconnect the device.")
    written = nb.write_pages(row, image)

    problem = describe_mismatch(nb.read_pages(row), after_write(image))
    if problem:
        print(f"Error: verification failed after writing {written} pages: {problem}",
              file=sys.stderr)
        print("The device cannot boot from NAND in this state. To restore the "
              "original bootloader:", file=sys.stderr)
        print(f"  opennboot restore --image {backup.name}", file=sys.stderr)
        return 1

    print("Installation complete.")
    return 0
