#!/usr/bin/env python3
import struct, sys
sys.path.insert(0, "usbboot/src")
from ak7802_usbboot.transport import find_device

STUB = "tools/sd-probe/stub/sd_noreset_probe.bin"

for i in range(5):
    dev = find_device()
    stub = open(STUB, "rb").read()
    dev.write_mem(0x48000240, stub)
    dev.execute(0x48000240, wait=True)
    raw = dev.read_mem(0x48001100, 20 * 4)
    w = struct.unpack("<20I", raw)

    sta_pre = w[3]
    r0 = w[4]
    sta8 = w[7]
    resp8 = w[6]

    ok = (sta8 & 0x10) != 0
    print(f"  [{i}] pre=0x{sta_pre:05X} CMD0=0x{r0&0xFFFF:04X}@{((r0>>16)&0xFFFF)} "
          f"CMD8={'OK' if ok else 'FAIL'}(0x{sta8:04X})", end="")
    if ok:
        print(f" resp=0x{resp8:08X}", end="")
        if w[8] != 0xBAD00001:
            ocr = w[14]
            print(f" OCR=0x{ocr:08X} ready={bool(ocr&0x80000000)}", end="")
    print()
