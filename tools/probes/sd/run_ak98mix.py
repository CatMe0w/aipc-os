#!/usr/bin/env python3
"""Set AK98 sharepin from host, then run noreset probe."""
import struct, sys, time
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

# Step 1: Set AK98 sharepin (bit29=1, GRP4=2)
dev = find_device()
dev.poke(0x08000078, 0x20070203)  # CON1: bit29=1, DATA[7:0]=7
dev.poke(0x08000074, 0x00000051)  # CON2: GRP4=2, GRP3=2, bit0=1
time.sleep(0.05)

# Step 2: Run noreset probe (doesn't touch CON1/CON2)
stub = open("tools/sd-probe/stub/sd_noreset_probe.bin", "rb").read()
dev2 = find_device()
dev2.write_mem(0x48000240, stub)
dev2.execute(0x48000240, wait=True)
w = struct.unpack("<20I", dev2.read_mem(0x48001100, 80))

sta_pre = w[3]
r0 = w[4]
sta8 = w[7]
resp8 = w[6]

print(f"pre=0x{sta_pre:05X} CMD0=0x{r0&0xFFFF:04X}@{((r0>>16)&0xFFFF)} "
      f"CMD8={'OK' if sta8&0x10 else 'FAIL'}(0x{sta8:04X})", end="")
if sta8 & 0x10:
    print(f" resp=0x{resp8:08X}", end="")
    if w[8] != 0xBAD00001:
        print(f" CMD55={w[8]} OCR=0x{w[14]:08X} ready={bool(w[14]&0x80000000)}", end="")
print()
