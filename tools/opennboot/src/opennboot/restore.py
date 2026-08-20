"""Put a bootloader image saved by `opennboot install` back into NAND."""

from __future__ import annotations

import sys
from pathlib import Path

from .nand import (
    BLOCK_PAGES, PAGE_RAW,
    NandBlock, NandError, after_write, confirm, connect, describe_mismatch,
)


def run(image_path: Path, row: int = 0) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    image = image_path.read_bytes()
    if len(image) != BLOCK_PAGES * PAGE_RAW:
        print(f"{image_path.name} is {len(image)} bytes, expected "
              f"{BLOCK_PAGES * PAGE_RAW}. This is not a backup from "
              f"`opennboot install`.", file=sys.stderr)
        return 1

    if row:
        print(f"[using scratch target: row {row}]")

    dev = connect()
    print("Device connected.")

    nb = NandBlock(dev)
    nb.check_ddr()

    if not confirm(f"This replaces the bootloader with {image_path.name}."):
        print("Aborted, nothing was written.")
        return 0

    print("Writing, do not disconnect.")
    written = nb.write_pages(row, image)

    problem = describe_mismatch(nb.read_pages(row), after_write(image))
    if problem:
        print(f"VERIFY FAILED after writing {written} pages: {problem}",
              file=sys.stderr)
        print("The device will not boot from NAND in this state. Try the same "
              "image again.", file=sys.stderr)
        return 1

    print(f"Done. {image_path.name} is restored.")
    return 0
