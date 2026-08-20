#!/usr/bin/env python3
"""Pad the SD test stub to a file of several clusters and stamp its length."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

TARGET_SIZE = 300000
LEN_OFFSET = 4              # `_img_len` in start.S


def filler(n: int) -> bytes:
    """Deterministic bytes from a linear congruential generator."""
    out = bytearray(n)
    x = 0x12345678
    for i in range(n):
        x = (1103515245 * x + 12345) & 0xFFFFFFFF
        out[i] = (x >> 16) & 0xFF
    return bytes(out)


def main() -> int:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stub", type=Path, default=here / "stub.bin")
    parser.add_argument("--out", type=Path, default=here / "BOOT.BIN")
    parser.add_argument("--size", type=int, default=TARGET_SIZE)
    args = parser.parse_args()

    stub = bytearray(args.stub.read_bytes())
    if len(stub) > args.size:
        print(f"stub is already {len(stub)} bytes, larger than {args.size}")
        return 1

    img = stub + bytearray(filler(args.size - len(stub)))
    struct.pack_into("<I", img, LEN_OFFSET, args.size)
    args.out.write_bytes(img)

    words = len(img) // 4
    total = sum(struct.unpack_from("<I", img, 4 * i)[0] for i in range(words))
    total &= 0xFFFFFFFF

    print(f"wrote {args.out} ({len(img)} bytes)")
    print("expected result block at 0x301E0000:")
    print(f"  +0x00 magic       0x42424E4F")
    print(f"  +0x04 CPSR        0x000000D3   (SVC, IRQ and FIQ masked)")
    print(f"  +0x0C entered at  0x33000000")
    print(f"  +0x10 length      {len(img):#010x}")
    print(f"  +0x14 word sum    {total:#010x}")
    print(f"  +0x18 done        0x600D600D")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
