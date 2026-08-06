#!/usr/bin/env python3
"""Read back the log openNBOOT left in memory.

DDR keeps its contents across a power cycle, so this recovers the log from the
previous run once the device is back in USB boot mode.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "tools" / "usbboot" / "src"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "ddr-init" / "src"))

from ak7802_usbboot.transport import find_device
from aipc_ddr_init.cli import ddr_init

from nfblock import FIRMWARE

LOG_BASE = 0x301F0000
LOG_SIZE = 0x00010000


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path,
                        help="also save the raw 64 KB buffer here")
    args = parser.parse_args()

    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    dev = find_device(wait=True)
    ddr_init(dev, firmware=FIRMWARE)
    print("Device connected.")

    data = dev.read_mem(LOG_BASE, LOG_SIZE)
    if args.out:
        args.out.write_bytes(data)
        print(f"Raw buffer saved to {args.out.name} ({len(data)} bytes)")

    text = data.rstrip(b"\x00")
    if not text:
        print("The log buffer is empty.")
        return 0

    print()
    print("--- BEGIN LOG ---")
    print(text.decode("ascii", errors="replace"), end="")
    if not text.endswith(b"\n"):
        print()
    print("--- END OF LOG ---")
    return 0


if __name__ == "__main__":
    sys.exit(main())
