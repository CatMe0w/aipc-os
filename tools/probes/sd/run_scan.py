import struct, sys
sys.path.insert(0, "tools/usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()
stub = open("tools/sd-probe/stub/sd_base_scan.bin", "rb").read()
print(f"stub: {len(stub)} bytes")
dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True)
raw = dev.read_mem(0x48001100, 128 * 4)
words = struct.unpack("<128I", raw)
print(f"  {'BASE':>12}  {'+0x04':>10}  {'+0x34':>10}  {'+0x0C':>10}  {'+0x08':>10}  {'+0x14':>10}  {'+0x00':>10}")
for slot in range(14):
    base = 4 + slot * 8
    b, v4, v34, vc, v8, v14, v0 = words[base:base+7]
    if v4 == 0 and v34 == 0 and vc == 0 and v8 == 0 and v14 == 0:
        continue
    print(f"  0x{b:08X}  0x{v4:08X}  0x{v34:08X}  0x{vc:08X}  0x{v8:08X}  0x{v14:08X}  0x{v0:08X}")
