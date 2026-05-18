#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()

# Set bootrom-style I/O control (bits 2-17, 26-27)
orig = struct.unpack("<I", dev.read_mem(0x080000D4, 4))[0]
new = orig | 0x0C03FFFC  # bits 2-17 + 26-27
dev.poke(0x080000D4, new)
v = struct.unpack("<I", dev.read_mem(0x080000D4, 4))[0]
print(f"SYSCTRL+0xD4 before: 0x{orig:08X} after: 0x{v:08X}")

# Run full init
stub = open("tools/sd-probe/stub/sd_full_init_v2.bin", "rb").read()
dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True)

w = struct.unpack("<8I", dev.read_mem(0x48001100, 32))
sta8 = w[7]
resp8 = w[6]
print(f"CMD8: sta=0x{sta8:04X} resp=0x{resp8:08X}", "OK!" if sta8 & 0x10 else "FAIL")
