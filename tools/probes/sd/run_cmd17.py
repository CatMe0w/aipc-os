#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, 'usbboot/src')
from ak7802_usbboot.transport import find_device

dev = find_device()
with open('tools/sd-probe/stub/sd_cmd17_v2.bin', 'rb') as f:
    dev.write_mem(0x48000240, f.read())
dev.execute(0x48000240, wait=True)

raw = dev.read_mem(0x48001100, 50*4)
words = struct.unpack('<50I', raw)

print(f'CMD8=0x{words[2]:08X} OCR=0x{words[4]:08X}')
if not (words[4] & 0x80000000):
    print('INIT FAIL')
    sys.exit(1)

print(f'CMD2=0x{words[42]:08X} RCA=0x{words[43]:08X} CMD7=0x{words[44]:08X}')
cr = words[6]
dr = words[45]
print(f'CMD17 cmd STA=0x{(cr&0xFFFF):04X} post-read STA=0x{dr:08X}')
b510, b511 = words[40], words[41]
ok = (b510 == 0x55 and b511 == 0xAA)
label = "VALID" if ok else "INVALID"
print(f'MBR sig: 0x{b510:02X}{b511:02X} {label}')
if ok:
    for i in range(4):
        vals = ' '.join(f'0x{words[8+i*4+j]:08X}' for j in range(4))
        print(f'  {vals}')
