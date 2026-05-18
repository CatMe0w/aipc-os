#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()

# Read power-related registers
for addr, name in [
    (0x080000D4, "SYSCTRL+0xD4 (I/O ctrl)"),
    (0x0800000C, "CLK_CTRL"),
    (0x080000BC, "GPIO1_IN (CD#)"),
    (0x080000C0, "GPIO2_IN"),
    (0x0800007C, "GPIO1_DIR"),
    (0x08000084, "GPIO2_DIR"),
    (0x0800009C, "PUPD1"),
    (0x080000A0, "PUPD2"),
    (0x20020004, "MCI_CLOCK"),
    (0x20020034, "MCI_STATUS"),
    (0x20020024, "MCI_DATATIMER"),
]:
    v = struct.unpack("<I", dev.read_mem(addr, 4))[0]
    print(f"  {name:30s} = 0x{v:08X}")

# Write SYSCTRL+0xD4 bit0 and re-read
print("\nWriting SYSCTRL+0xD4 |= 1 ...")
orig = struct.unpack("<I", dev.read_mem(0x080000D4, 4))[0]
dev.poke(0x080000D4, orig | 1)
after = struct.unpack("<I", dev.read_mem(0x080000D4, 4))[0]
print(f"  Before: 0x{orig:08X}  After: 0x{after:08X}")
