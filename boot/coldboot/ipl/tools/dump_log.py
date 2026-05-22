#!/usr/bin/env python3
"""Read back the IPL memory log via cold-boot dump.

UART on the dev device is dead, so IPL writes diagnostics to a 64 KB
buffer at 0x31D00000 (same convention as doom/src/syscalls.c). After the IPL
runs and halts, the user forces the device back into bootrom usbboot mode
(DL_JUMP / USB_BOOT pin + USB replug). This script re-initializes the DDR
controller (which preserves cell contents) and reads the log buffer back.

Run from the ipl directory:
    uv run tools/dump_log.py --firmware 1.58.2
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

LOG_BASE = 0x31D00000
LOG_SIZE = 0x00010000


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, choices=("1.58.2", "1.88"),
                        help="DDR init script variant matching the target device")
    parser.add_argument("--raw", type=Path,
                        help="Optional path to dump the raw 64 KB buffer")
    args = parser.parse_args()

    print("Waiting for device in USB boot mode...")
    dev = find_device(wait=True)

    label, _ = ddr_init(dev, firmware=args.firmware)
    print(f"DDR re-initialized ({label})")

    data = dev.read_mem(LOG_BASE, LOG_SIZE)
    if args.raw:
        args.raw.write_bytes(data)
        print(f"raw buffer -> {args.raw}")

    text = data.rstrip(b"\x00")
    if not text:
        print("(log buffer is empty)")
        return 0

    try:
        print(text.decode("ascii"), end="")
    except UnicodeDecodeError:
        print(text.decode("ascii", errors="replace"), end="")
    if not text.endswith(b"\n"):
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
