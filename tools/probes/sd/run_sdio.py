import struct, sys
sys.path.insert(0, "tools/usbboot/src")
from ak7802_usbboot.transport import find_device

dev = find_device()
stub = open("tools/sd-probe/stub/sd_sdio_probe.bin", "rb").read()
print(f"stub: {len(stub)} bytes")
dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True)
raw = dev.read_mem(0x48001100, 16 * 4)
words = struct.unpack("<16I", raw)
for i, w in enumerate(words):
    print(f"  [{i:2d}] = 0x{w:08X}")
