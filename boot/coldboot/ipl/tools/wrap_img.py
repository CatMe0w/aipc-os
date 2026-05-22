#!/usr/bin/env python3
"""Wrap ipl.bin with the 44-byte IMG header used by the IPL NAND partition.

Header layout (offsets relative to start of IPL.raw partition slice):

    +0x00  4   magic         "IMG\\0"
    +0x04  4   tag           "IPL\\0"
    +0x08  16  filename      "eboot.nb0\\0..." (NUL-padded)
    +0x18  4   eboot-private  (not parsed by nboot; differs between firmware versions)
    +0x1C  4   size_advertised
    +0x20  4   load_addr      0x80038000
    +0x24  4   size_advertised (duplicated)
    +0x28  4   count          1

The first payload instruction lands at IPL.raw + 0x2C, which nboot maps to
physical 0x30038000 by loading the partition slice at 0x30037FD4. nboot itself
does NOT parse this header (it uses hardcoded constants), so values beyond the
ones we set explicitly are mostly cosmetic.

Run from the ipl directory:
    uv run tools/wrap_img.py
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
DEFAULT_IN  = REPO_ROOT / "boot" / "coldboot" / "ipl" / "ipl.bin"
DEFAULT_OUT = REPO_ROOT / "boot" / "coldboot" / "ipl" / "ipl.img"

LOAD_ADDR        = 0x80038000
SIZE_ADVERTISED  = 0x00080000
HEADER_LEN       = 0x2C
NBOOT_LOAD_LIMIT = 0x64000


def build_header() -> bytes:
    filename = b"eboot.nb0".ljust(16, b"\x00")
    return (
        b"IMG\x00" +
        b"IPL\x00" +
        filename +
        struct.pack("<I", 0) +
        struct.pack("<I", SIZE_ADVERTISED) +
        struct.pack("<I", LOAD_ADDR) +
        struct.pack("<I", SIZE_ADVERTISED) +
        struct.pack("<I", 1)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--in", dest="src", type=Path, default=DEFAULT_IN,
                        help="Input ipl.bin (default: boot/coldboot/ipl/ipl.bin)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help="Output ipl.img (default: boot/coldboot/ipl/ipl.img)")
    args = parser.parse_args()

    if not args.src.exists():
        print(f"input not found: {args.src}", file=sys.stderr)
        return 1

    payload = args.src.read_bytes()
    header = build_header()
    assert len(header) == HEADER_LEN

    total = len(header) + len(payload)
    if total > NBOOT_LOAD_LIMIT:
        print(f"ipl.img total {total:#x} exceeds nboot 0x64000 load limit",
              file=sys.stderr)
        return 1

    args.out.write_bytes(header + payload)
    print(f"wrote {args.out} ({total} bytes, payload {len(payload)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
