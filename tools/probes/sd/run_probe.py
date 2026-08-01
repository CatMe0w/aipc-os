#!/usr/bin/env python3
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB_ADDR = 0x48000240
RESULT_ADDR = 0x48001100
PARAM_ADDR = 0x48001380
MAGIC = 0x53445052
READ_LBA_MAGIC = 0x52454144
WRITE_LBA_MAGIC = 0x57524954


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--words", type=int, default=64)
    lba = parser.add_mutually_exclusive_group()
    lba.add_argument("--lba", type=lambda value: int(value, 0))
    lba.add_argument("--write-lba", type=lambda value: int(value, 0))
    args = parser.parse_args()

    image = args.binary.read_bytes()
    if len(image) > 0xC30:
        raise SystemExit("stub exceeds executable L2 SRAM range")

    print(f"stub={args.binary} size={len(image)}")
    dev = find_device()
    dev.write_mem(STUB_ADDR, image)
    param_magic = 0
    param_lba = 0
    if args.lba is not None:
        param_magic = READ_LBA_MAGIC
        param_lba = args.lba
    elif args.write_lba is not None:
        param_magic = WRITE_LBA_MAGIC
        param_lba = args.write_lba
    if not 0 <= param_lba <= 0xFFFFFFFF:
        raise SystemExit("LBA exceeds the 32-bit probe parameter")
    dev.write_mem(PARAM_ADDR, struct.pack("<II", param_magic, param_lba))
    if param_magic:
        print(f"lba={param_lba} write={param_magic == WRITE_LBA_MAGIC}")
    dev.execute(STUB_ADDR, wait=True, timeout=10.0)
    raw = dev.read_mem(RESULT_ADDR, args.words * 4)
    words = struct.unpack(f"<{args.words}I", raw)

    for i in range(0, len(words), 4):
        values = " ".join(f"{word:08x}" for word in words[i:i + 4])
        print(f"{i:03d}: {values}")

    if len(words) < 5 or words[0] != MAGIC:
        print("invalid result header", file=sys.stderr)
        return 2
    print(f"version={words[1]} experiment={words[2]} status={words[3]} payload_words={words[4]}")
    return 0 if words[3] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
