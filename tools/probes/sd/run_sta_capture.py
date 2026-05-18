#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

STUB = "tools/sd-probe/stub/sd_sta_capture.bin"

dev = find_device()
stub = open(STUB, "rb").read()
print(f"stub: {len(stub)} bytes")
dev.write_mem(0x48000240, stub)
dev.execute(0x48000240, wait=True)

raw = dev.read_mem(0x48001100, 48 * 4)
w = struct.unpack("<48I", raw)

print(f"  [0] magic     = 0x{w[0]:08X}")
print(f"  [1] version   = {w[1]}")
print(f"  [2] PUPD2     = 0x{w[2]:08X}")
print(f"  [3] CLOCK     = 0x{w[3]:08X}")
print(f"  [4] STA(b4cmd)= 0x{w[4]:08X}")

# CMD0: samples
print(f"\n  --- CMD0 (CPSM_ENABLE, opcode=0) ---")
for n in range(16):
    print(f"  STA samp[{n:2d}] = 0x{w[10+n]:08X}")

r0 = w[5]
print(f"  CMD0 wait: iter={((r0>>16)&0xFFFF):5d}, STA=0x{(r0&0xFFFF):04X}")
print(f"  STA(post)  = 0x{w[6]:08X}")
print(f"  CMD rdbk   = 0x{w[7]:08X}")

# CMD8: samples
print(f"\n  --- CMD8 (CPSM_ENABLE|CPSM_RESPONSE|(8<<1), arg=0x1AA) ---")
for n in range(16):
    print(f"  STA samp[{n:2d}] = 0x{w[26+n]:08X}")

r8 = w[8]
print(f"  CMD8 wait: iter={((r8>>16)&0xFFFF):5d}, STA=0x{(r8&0xFFFF):04X}")
print(f"  STA(post)  = 0x{w[9]:08X}")
print(f"  CMD rdbk   = 0x{w[42]:08X}")
print(f"  RESP0      = 0x{w[43]:08X}")
print(f"  RESPCMD    = 0x{w[44]:08X}")

# Interpret CMD8
sta8 = r8 & 0xFFFF
resp_end = bool(sta8 & 0x10)
resp_timeo = bool(sta8 & 0x04)
resp_crc = bool(sta8 & 0x01)
cmd_sent = bool(sta8 & 0x20)
print(f"\n  CMD8: RESP_END={resp_end}, RESP_TIMEO={resp_timeo}, RESP_CRC={resp_crc}, CMD_SENT={cmd_sent}")
if resp_end:
    print(f"  *** CARD RESPONDED! ***")
elif resp_timeo:
    print(f"  CARD DID NOT RESPOND (timeout)")
