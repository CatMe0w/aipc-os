#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()

# Use simple 32-bit poke to test MCI+0x24
MCI = 0x20020000

# Read before
v_before = struct.unpack("<I", dev.read_mem(MCI+0x24, 4))[0]
print(f"MCI+0x24 before write: 0x{v_before:08X}")

# Write 0x30000
dev.poke(MCI+0x24, 0x00030000)

# Read after
v_after = struct.unpack("<I", dev.read_mem(MCI+0x24, 4))[0]
print(f"MCI+0x24 after  write: 0x{v_after:08X}")

# Try other potential DATATIMER offsets
for off in [0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C]:
    v = struct.unpack("<I", dev.read_mem(MCI+off, 4))[0]
    print(f"MCI+0x{off:02X}: 0x{v:08X}")

# Also check MCI_CLOCK and MCI_STA for sanity
clk = struct.unpack("<I", dev.read_mem(MCI+0x04, 4))[0]
sta = struct.unpack("<I", dev.read_mem(MCI+0x34, 4))[0]
print(f"MCI_CLOCK:  0x{clk:08X}")
print(f"MCI_STATUS: 0x{sta:08X}")

# Try writing 0xFFFFFFFF and reading back to get writable mask
for off in [0x20, 0x24, 0x28, 0x2C, 0x30]:
    orig = struct.unpack("<I", dev.read_mem(MCI+off, 4))[0]
    dev.poke(MCI+off, 0xFFFFFFFF)
    after = struct.unpack("<I", dev.read_mem(MCI+off, 4))[0]
    dev.poke(MCI+off, orig)  # restore
    print(f"MCI+0x{off:02X}: writable_mask=0x{after:08X} (orig=0x{orig:08X})")
