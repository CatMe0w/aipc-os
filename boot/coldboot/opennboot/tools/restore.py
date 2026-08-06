#!/usr/bin/env python3
"""Put a bootloader image saved by install.py back into NAND."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "tools" / "usbboot" / "src"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "ddr-init" / "src"))

from ak7802_usbboot.transport import find_device
from aipc_ddr_init.cli import ddr_init

from nfblock import (
    BLOCK_PAGES, FIRMWARE, PAGE_RAW,
    NandBlock, NandError, after_write, confirm, describe_mismatch,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True,
                        help="the backup_nboot-*.bin file install.py saved")
    parser.add_argument("--row", type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args()

    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    image = args.image.read_bytes()
    if len(image) != BLOCK_PAGES * PAGE_RAW:
        print(f"{args.image.name} is {len(image)} bytes, expected "
              f"{BLOCK_PAGES * PAGE_RAW}. This is not a backup from install.py.",
              file=sys.stderr)
        return 1

    if args.row:
        print(f"[using scratch target: row {args.row}]")

    dev = find_device(wait=True)
    ddr_init(dev, firmware=FIRMWARE)
    print("Device connected.")

    nb = NandBlock(dev)
    nb.check_ddr()

    if not confirm(f"This replaces the bootloader with {args.image.name}."):
        print("Aborted, nothing was written.")
        return 0

    print("Writing, do not disconnect.")
    written = nb.write_pages(args.row, image)

    problem = describe_mismatch(nb.read_pages(args.row), after_write(image))
    if problem:
        print(f"VERIFY FAILED after programming {written} pages: {problem}",
              file=sys.stderr)
        return 1

    print(f"Done. {args.image.name} is restored successfully.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except NandError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
