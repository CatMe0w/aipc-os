#!/usr/bin/env python3
"""Read back the trace the gdbstub BOOT.BIN left in memory."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "usbboot" / "src"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "ddr-init" / "src"))

from ak7802_usbboot.transport import find_device
from aipc_ddr_init.cli import ddr_init

TRACE_BASE = 0x301C0000
TRACE_PREV = 0x301D0000
TRACE_SIZE = 0x00010000
OPENNBOOT_LOG_BASE = 0x301F0000
OPENNBOOT_LOG_SIZE = 0x00010000


def show(title: str, data: bytes) -> None:
    text = data.rstrip(b"\x00")
    print()
    print(f"--- {title} ---")
    if not text:
        print("(empty)")
        return
    print(text.decode("ascii", errors="replace"), end="")
    if not text.endswith(b"\n"):
        print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path,
                        help="also save the raw 64 KB trace buffer here")
    parser.add_argument("--opennboot", action="store_true",
                        help="also dump openNBOOT's log")
    parser.add_argument("--firmware", default="1.88",
                        help="DDR init stub variant (default: 1.88)")
    args = parser.parse_args()

    sys.stdout.reconfigure(line_buffering=True)  # pyright: ignore[reportAttributeAccessIssue]

    dev = find_device(wait=True)
    ddr_init(dev, firmware=args.firmware)
    print("Device connected.")

    data = dev.read_mem(TRACE_BASE, TRACE_SIZE)
    if args.out:
        args.out.write_bytes(data)
        print(f"Raw buffer saved to {args.out.name} ({len(data)} bytes)")

    show("gdbstub trace (previous run)", dev.read_mem(TRACE_PREV, TRACE_SIZE))
    show("gdbstub trace", data)
    if args.opennboot:
        show("opennboot log",
             dev.read_mem(OPENNBOOT_LOG_BASE, OPENNBOOT_LOG_SIZE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
