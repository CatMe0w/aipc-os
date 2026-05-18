#!/usr/bin/env python3
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB = Path(__file__).resolve().parent / "stub" / "sd_full_init_v2.bin"
stub = STUB.read_bytes()

for attempt in range(1, 4):
    print(f"\n=== Attempt {attempt} ===")
    dev = find_device()
    dev.write_mem(0x48000240, stub)
    dev.execute(0x48000240, wait=True)
    raw = dev.read_mem(0x48001100, 20 * 4)
    w = struct.unpack("<20I", raw)
    
    magic = w[0]
    print(f"  magic=0x{w[0]:08X}")
    if magic != 0x46494E32:
        print("  BAD MAGIC")
        break
    
    # CMD0
    r0 = w[4]
    sta0 = r0 & 0xFFFF
    iter0 = (r0 >> 16) & 0xFFFF
    print(f"  CMD0: iter={iter0}, STA=0x{sta0:04X} ({'CMD_SENT' if sta0&0x20 else '?'})")
    
    # CMD8
    rc8 = w[5]
    sta8 = w[7]
    resp8 = w[6]
    if rc8 == 0xFFFFFFFF:
        print(f"  CMD8: RESP_TIMEO (sta=0x{sta8:04X})")
    elif rc8 == 0:
        print(f"  CMD8: OK resp=0x{resp8:08X} sta=0x{sta8:04X}")
        if resp8 == 0x1AA:
            print(f"  *** CARD ALIVE! ***")
    else:
        print(f"  CMD8: rc={rc8}, resp=0x{resp8:08X}, sta=0x{sta8:04X}")
    
    ocr = w[14] if w[8] != 0xBAD00001 else 0
    if ocr:
        print(f"  OCR=0x{ocr:08X} ready={bool(ocr&0x80000000)}")
    else:
        print(f"  (no OCR)")
