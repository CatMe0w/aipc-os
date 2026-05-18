#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()
print("GPIO1_IN  (0xBC):", hex(struct.unpack("<I", dev.read_mem(0x080000BC, 4))[0]))
print("GPIO1_DIR (0x7C):", hex(struct.unpack("<I", dev.read_mem(0x0800007C, 4))[0]))
print("GPIO2_IN  (0xC0):", hex(struct.unpack("<I", dev.read_mem(0x080000C0, 4))[0]))
print("PUPD2     (0xA0):", hex(struct.unpack("<I", dev.read_mem(0x080000A0, 4))[0]))
print("CON1      (0x78):", hex(struct.unpack("<I", dev.read_mem(0x08000078, 4))[0]))
print("CON2      (0x74):", hex(struct.unpack("<I", dev.read_mem(0x08000074, 4))[0]))
print("CLK_CTRL  (0x0C):", hex(struct.unpack("<I", dev.read_mem(0x0800000C, 4))[0]))
v = struct.unpack("<I", dev.read_mem(0x080000BC, 4))[0]
print(f"SD_CD# (bit13) = {(v>>13)&1}  (0=card detected, 1=no card)")
