#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()

for off, name in [
    (0x00,"INTR_CTRL"), (0x04,"CLOCK"), (0x08,"ARG"), (0x0C,"CMD"),
    (0x10,"RESPCMD"), (0x14,"RESP0"), (0x18,"RESP1"), (0x1C,"RESP2"),
    (0x20,"RESP3"), (0x24,"DATATIMER"), (0x28,"DATALEN"), (0x2C,"DATACTRL"),
    (0x30,"?0x30"), (0x34,"STATUS"), (0x38,"MASK"), (0x3C,"DMACTRL"),
    (0x40,"FIFO"),
]:
    v = struct.unpack("<I", dev.read_mem(0x20020000 + off, 4))[0]
    print(f"  MCI+0x{off:02X} {name:12s} = 0x{v:08X}")

sta = struct.unpack("<I", dev.read_mem(0x20020034, 4))[0]
print(f"\n  STATUS = 0x{sta:08X}  bits set:", end="")
for b in range(32):
    if sta & (1 << b):
        print(f" {b}", end="")
print()
