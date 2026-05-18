#!/usr/bin/env python3
"""Reproduce the exact sequence that worked: AK98 sharepin first, then AK7802 init."""
import struct, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB_DIR = Path(__file__).resolve().parent / "stub"
STUB = STUB_DIR / "sd_full_init_v2.bin"
stub = STUB.read_bytes()

# Step 1: set AK98-style sharepin (bit29=1, GRP4=MMC) like sd_sharepin_probe does
print("=== Step 1: Set AK98-style sharepin ===")
dev = find_device()
# CON1 bit29=1 (MDAT2), CON2 GRP4=2 (MMC 4-bit)
dev.poke(0x08000078, 0x20070203)  # CON1: bit29=1, bits[18:16]=7
dev.poke(0x08000074, 0x00000051)  # CON2: GRP4=2, GRP3=2, bit0=1
time.sleep(0.1)
v = struct.unpack("<I", dev.read_mem(0x08000078, 4))[0]
print(f"  CON1 = 0x{v:08X}")
v = struct.unpack("<I", dev.read_mem(0x08000074, 4))[0]
print(f"  CON2 = 0x{v:08X}")

# Step 2: immediate second connect and full init
print("\n=== Step 2: Run sd_full_init_v2 ===")
dev2 = find_device()
dev2.write_mem(0x48000240, stub)
dev2.execute(0x48000240, wait=True)
raw = dev2.read_mem(0x48001100, 20 * 4)
w = struct.unpack("<20I", raw)

print(f"  magic=0x{w[0]:08X}")
r0 = w[4]
print(f"  CMD0: iter={(r0>>16)&0xFFFF}, STA=0x{(r0&0xFFFF):04X}")
rc8 = w[5]
sta8 = w[7]
resp8 = w[6]
if rc8 == 0:
    print(f"  CMD8: OK resp=0x{resp8:08X} sta=0x{sta8:04X}")
    if resp8 == 0x1AA:
        print(f"  *** SUCCESS! Card responds! ***")
        # Check CMD55/ACMD41
        if w[8] != 0xBAD00001:
            print(f"  CMD55 rc={w[8]}, resp=0x{w[9]:08X}")
            if w[11] != 0xBAD55000 and w[14] != 0xBAD41001:
                print(f"  ACMD41 OCR=0x{w[12]:08X} ready={bool(w[12]&0x80000000)}")
                if w[14] and w[14] != w[12]:
                    print(f"  Final OCR=0x{w[14]:08X} attempts={w[16]}")
else:
    print(f"  CMD8: FAILED rc={rc8}, sta=0x{sta8:04X}")

# Step 3: second attempt (like the successful run was the 2nd)
print("\n=== Step 3: Second sd_full_init_v2 (should succeed?) ===")
dev3 = find_device()
dev3.write_mem(0x48000240, stub)
dev3.execute(0x48000240, wait=True)
raw = dev3.read_mem(0x48001100, 20 * 4)
w = struct.unpack("<20I", raw)

rc8 = w[5]
sta8 = w[7]
resp8 = w[6]
if rc8 == 0:
    print(f"  CMD8: OK resp=0x{resp8:08X} sta=0x{sta8:04X}")
    if resp8 == 0x1AA:
        print(f"  *** SUCCESS! ***")
        if w[8] != 0xBAD00001:
            print(f"  CMD55 rc={w[8]}, resp=0x{w[9]:08X}")
            if w[11] != 0xBAD55000 and w[14] != 0xBAD41001:
                print(f"  ACMD41 OCR=0x{w[14]:08X} ready={bool(w[14]&0x80000000)}")
else:
    print(f"  CMD8: FAILED sta=0x{sta8:04X}")
