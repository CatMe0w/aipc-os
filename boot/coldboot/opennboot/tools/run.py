#!/usr/bin/env python3
"""Run openNBOOT from memory without installing it.

Uploads opennboot.bin to DDR and executes it in place, so the whole boot flow
can be exercised without writing to NAND. The bootrom does not resume
afterwards; the log survives the next power cycle and dump_log.py reads it back.
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

from nfblock import FIRMWARE, PAYLOAD

LOAD_ADDR = 0x30000000


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    # Only meaningful when testing a relocated build, so it stays out of --help.
    parser.add_argument("--addr", type=lambda s: int(s, 0), default=LOAD_ADDR,
                        help=argparse.SUPPRESS)
    args = parser.parse_args()

    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    if not PAYLOAD.exists():
        print(f"Not built: {PAYLOAD}", file=sys.stderr)
        print("Build it with: make -C boot/coldboot/opennboot", file=sys.stderr)
        return 1
    payload = PAYLOAD.read_bytes()

    print("openNBOOT dry run")
    print("This runs openNBOOT from memory. Nothing will be written to NAND.")
    if args.addr != LOAD_ADDR:
        print(f"[using load address {args.addr:#010x}]")
    print()

    dev = find_device(wait=True)
    ddr_init(dev, firmware=FIRMWARE)
    print("Device connected.")

    dev.write_mem(args.addr, payload)
    print(f"Uploaded openNBOOT ({len(payload)} bytes)")

    dev.execute(args.addr, wait=False)
    print("Starting openNBOOT. The bootrom will not resume.")
    print()
    print("  To read the log:  uv run tools/dump_log.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
