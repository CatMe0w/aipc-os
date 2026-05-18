#!/usr/bin/env python3
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB = Path(__file__).resolve().parent / "stub" / "sd_acmd41_loop.bin"

dev = find_device()
stub = STUB.read_bytes()
print(f"stub: {len(stub)} bytes")
dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True, timeout=10.0)

raw = dev.read_mem(0x48001100, 20 * 4)
w = struct.unpack("<20I", raw)

print(f"  [0] magic      = 0x{w[0]:08X}")
print(f"  [1] version    = {w[1]}")
print(f"  [2] CMD8 resp  = 0x{w[2]:08X}")
print(f"  [3] CMD8 sta   = 0x{w[3]:08X}")

if w[2] == 0xBAD00001:
    print("  CMD8: FAILED")
elif w[2] == 0x1AA:
    print("  CMD8: OK (0x1AA)")
else:
    print(f"  CMD8: unexpected resp=0x{w[2]:08X}")

if w[4] == 0x544F5554:
    print(f"  ACMD41: TIMEOUT after {w[1]} attempts")
elif (w[4] & 0xFFFF0000) == 0xBAD40000 or (w[4] & 0xFFFF0000) == 0xBAD50000:
    print(f"  ACMD41: FAILED at attempt {w[4]&0xFFFF}")
else:
    attempt = w[4]
    ocr = w[5]
    print(f"  ACMD41: ready after {attempt} attempts")
    print(f"  OCR      = 0x{ocr:08X}")
    card_type = "SDHC" if (ocr & 0x40000000) else "SDSC"
    print(f"  Type     = {card_type}")
    print(f"  Voltage  = 0x{(ocr>>16)&0xFF:02X}")
    if w[6]:
        print(f"  Feedback arg = 0x{w[6]:08X}")
    if w[7]:
        print(f"  Card type tag = 0x{w[7]:08X}")
    if w[8]:
        print(f"  OCR voltage   = 0x{w[8]:08X}")
    if w[9]:
        print(f"  STA after     = 0x{w[9]:08X}")
    if w[10]:
        print(f"  No-HCS arg    = 0x{w[10]:08X}")
    if w[11]:
        print(f"  Status        = 0x{w[11]:08X}")
