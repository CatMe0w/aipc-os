"""Install openNBOOT into NAND block 0, in place of the bootloader that shipped
with the device."""

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

# One header page plus payload, all inside block 0.
MAX_PAYLOAD_PAGES = BLOCK_PAGES - 1


def build_image(original: bytes, payload: bytes) -> tuple[bytes, int]:
    header = original[:PAGE_DATA]
    if header[SIG_OFFSET:SIG_OFFSET + 8] != SIGNATURE:
        raise NandError(
            "The header page does not carry the ANYKA382 signature. The bootrom "
            "would reject the result, so nothing was written"
        )

    declared = header[CHUNKS_OFFSET] * 512
    if declared != PAGE_DATA:
        raise NandError(
            f"The header declares a {declared} byte page. These tools are "
            f"built around {PAGE_DATA}"
        )

    installed = header[PAGE_COUNT_OFFSET]
    if installed > MAX_PAYLOAD_PAGES:
        raise NandError(
            f"The installed bootloader claims {installed} pages, past the "
            f"{MAX_PAYLOAD_PAGES} this tool reads and saves. It cannot get a "
            f"complete backup"
        )

    page_count = (len(payload) + PAGE_DATA - 1) // PAGE_DATA
    if page_count > MAX_PAYLOAD_PAGES:
        raise NandError(
            f"Payload needs {page_count} pages, more than the "
            f"{MAX_PAYLOAD_PAGES} that fit in one block"
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
        raise NandError(f"{path} already exists. This tool will not overwrite it")
    path.write_bytes(original)
    problem = describe_mismatch(path.read_bytes(), original)
    if problem:
        raise NandError(
            f"{path} does not match what was read from the device: {problem}"
        )


def run(row: int = 0) -> int:
    # Progress must reach the user during the write, not when the buffer fills.
    # A "do not disconnect" line is worthless after the write.
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    source = resolve("opennboot.bin")
    payload = source.read_bytes()
    backup = Path.cwd() / f"backup_nboot-{datetime.now():%Y%m%d-%H%M%S}.bin"

    print("openNBOOT installer")
    print("This replaces the bootloader your device shipped with. It saves the "
          "current one first.")
    print(f"Image: {source} ({len(payload)} bytes)")
    if row:
        print(f"  [using scratch target: row {row}]")
    print()

    dev = connect()
    print("Device connected.")

    nb = NandBlock(dev)
    nb.check_ddr()

    original = nb.read_pages(row)
    save_backup(original, backup)
    print(f"Current bootloader saved to {backup.name} ({len(original)} bytes)")
    print()
    print("  [!] Keep this file. It is the only copy of the bootloader your "
          "device shipped with.")
    print(f"  To undo this install:  opennboot restore --image {backup.name}")
    print()

    image, _ = build_image(original, payload)

    if not confirm("This erases and replaces the bootloader."):
        print("Aborted, nothing was written. The backup above is still intact.")
        return 0

    print("Writing, do not disconnect.")
    written = nb.write_pages(row, image)

    problem = describe_mismatch(nb.read_pages(row), after_write(image))
    if problem:
        print(f"VERIFY FAILED after writing {written} pages: {problem}",
              file=sys.stderr)
        print("The device will not boot from NAND in this state. Restore the "
              "original bootloader with:", file=sys.stderr)
        print(f"  opennboot restore --image {backup.name}", file=sys.stderr)
        return 1

    print("Done. openNBOOT is installed.")
    return 0
