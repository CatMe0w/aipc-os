#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB_PATH = Path(__file__).resolve().parent / "stub" / "sd_full_init_v2.bin"

dev = find_device()
stub = STUB_PATH.read_bytes()
print(f"stub: {len(stub)} bytes")

dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True)

raw = dev.read_mem(0x48001100, 20 * 4)
words = struct.unpack("<20I", raw)

for i, w in enumerate(words):
    print(f"  [{i:2d}] = 0x{w:08X}")

# Interpret results
magic = words[0]
print(f"\n  magic = {magic:#010x} {'OK' if magic == 0x46494E32 else 'BAD'}")
print(f"  version = {words[1]}")
print(f"  CLOCK after init = 0x{words[2]:08X}")
print(f"  STA before CMD0 = 0x{words[3]:08X}")
print(f"  CMD0 sta_cap = 0x{words[4]:08X} (iter={(words[4]>>16)&0xFFFF}, sta=0x{(words[4]&0xFFFF):04X})")

rc8 = words[5]
resp8 = words[6]
sta8 = words[7]
print(f"  CMD8 rc={rc8}, resp=0x{resp8:08X}, sta=0x{sta8:04X}")
if rc8 == 0 and resp8 == 0x1AA:
    print("  CMD8: OK (card responds to 2.7-3.6V)")
elif words[8] == 0xBAD00001:
    print("  CMD8: FAILED")

rc55 = words[8] if magic == 0x46494E32 and words[8] != 0xBAD00001 else None
if rc55 is not None:
    print(f"  CMD55 rc={rc55}, resp=0x{words[9]:08X}, sta=0x{words[10]:04X}")

rc41 = words[11] if magic == 0x46494E32 and words[11] != 0xBAD55000 else None
if rc41 is not None and rc41 >= 0:
    ocr = words[12]
    sta41 = words[13]
    print(f"  ACMD41 rc={rc41}, OCR=0x{ocr:08X}, sta=0x{sta41:04X}")
    if ocr & 0x80000000:
        card_type = "SDHC" if (ocr & 0x40000000) else "SDSC"
        voltage = (ocr >> 16) & 0xFF
        print(f"  Card ready! type={card_type}, voltage=0x{voltage:02X}")
        if words[14] == ocr:
            print(f"  (card was already ready on first ACMD41)")
    else:
        print(f"  Card NOT ready after {words[16]} ACMD41 attempts")
        print(f"  Final OCR=0x{words[14]:08X}")
elif words[11] == 0xBAD55000:
    print(f"  CMD55: FAILED")
elif words[14] == 0xBAD41001:
    print(f"  ACMD41: FAILED")
