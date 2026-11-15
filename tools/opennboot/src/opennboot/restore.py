from __future__ import annotations

import sys
from pathlib import Path

from .nand import BLOCK_PAGES, PAGE_RAW, Block0, WriteFailed, confirm, connect


def run(image_path: Path, row: int = 0) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    image = image_path.read_bytes()
    if len(image) != BLOCK_PAGES * PAGE_RAW:
        print(f"Error: {image_path.name}: invalid size {len(image)} bytes "
              f"(expected {BLOCK_PAGES * PAGE_RAW}). This is not a backup created by "
              f"'opennboot install'", file=sys.stderr)
        return 1

    if row:
        print(f"Scratch target: row {row}.")

    dev = connect()
    print("Device connected.")

    b0 = Block0(dev)
    b0.check(image, image_path.name)

    if not confirm(f"The bootloader in NAND block 0 will be erased and "
                   f"replaced with {image_path.name}."):
        print("Aborted. The NAND was not modified.")
        return 0

    print("Writing to NAND. Do not disconnect the device.")
    try:
        b0.write(row)
    except WriteFailed as exc:
        print(f"Error: {exc}", file=sys.stderr)
        print("The device may not boot from NAND in this state. Run the same "
              "command again.", file=sys.stderr)
        return 1

    print("Restore complete.")
    return 0
