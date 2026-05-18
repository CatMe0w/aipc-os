#!/usr/bin/env python3
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB = Path(__file__).resolve().parent / "stub" / "sd_full_init_v2.bin"
stub = STUB.read_bytes()

ok = 0
total = 0
for i in range(10):
    dev = find_device()
    dev.write_mem(0x48000240, stub)
    dev.execute(0x48000240, wait=True)
    raw = dev.read_mem(0x48001100, 8 * 4)
    w = struct.unpack("<8I", raw)
    total += 1
    sta8 = w[7]
    if sta8 & 0x10:  # RESP_END
        ok += 1
        print(f"  [{i}] OK  (sta=0x{sta8:04X}, resp=0x{w[6]:08X})")
    else:
        print(f"  [{i}] FAIL (sta=0x{sta8:04X})")

print(f"\n{ok}/{total} succeeded")
