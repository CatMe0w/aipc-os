#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

STUB = "tools/sd-probe/stub/sd_full_flow.bin"

dev = find_device()
stub = open(STUB, "rb").read()
print(f"stub: {len(stub)} bytes")
dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True, timeout=10.0)

raw = dev.read_mem(0x48001100, 48 * 4)
w = struct.unpack("<48I", raw)

print(f"  magic       = 0x{w[0]:08X}")
print(f"  MCI_CLOCK   = 0x{w[2]:08X}")

rc8 = w[4]
resp8 = w[5]
print(f"  CMD8: rc={rc8} resp=0x{resp8:08X} {'OK' if rc8==0 else 'FAIL'}")
if w[7] == 0xBAD8: sys.exit(1)

print(f"  ACMD41: attempts={w[7]} OCR=0x{w[8]:08X} ready={bool(w[8]&0x80000000)}")
if w[10] == 0xBAD41: sys.exit(1)

r2 = w[11]
sta2 = r2 & 0xFFFF
print(f"  CMD2: iter={((r2>>16)&0xFFFF)}, STA=0x{sta2:04X} {'OK' if sta2&0x10 else 'FAIL'}")
print(f"  CMD3: RCA=0x{w[16]:08X}")
print(f"  CMD7: resp=0x{w[17]:08X}")

# CMD17
r17 = w[18]
sta17 = r17 & 0xFFFF
print(f"  CMD17: iter={((r17>>16)&0xFFFF)} STA=0x{sta17:04X} {'RESP_END' if sta17&0x10 else 'TIMEOUT'}")
if w[19] == 0xBAD17C:
    print("  CMD17 FAILED")
else:
    print(f"  CMD17 RESP0=0x{w[19]:08X}")
    print(f"  First 4 L2 words: 0x{w[20]:08X} 0x{w[21]:08X} 0x{w[22]:08X} 0x{w[23]:08X}")
    print(f"  Last word (bytes 508-511): 0x{w[35]:08X}")
    print(f"  STA(post) = 0x{w[37]:08X}")
    b510 = w[38]; b511 = w[39]
    print(f"  MBR sig: 0x{b510:02X}{b511:02X} {'VALID' if b510==0x55 and b511==0xAA else 'INVALID'}")
