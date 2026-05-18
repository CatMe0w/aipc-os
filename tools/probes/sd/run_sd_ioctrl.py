import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB_ADDR   = 0x48000240
RESULT_ADDR = 0x48001100
STUB_PATH   = Path(__file__).resolve().parent / "stub" / "sd_ioctrl_probe.bin"

FIELD_NAMES = [
    "magic",
    "clk_con1",
    "io_ctrl_before",
    "pupd1_before",
    "pupd2_before",
    "sta_before",
    "sharepin_con1",
    "sharepin_con2",
    "pupd1_after",
    "pupd2_after",
    "io_ctrl_after",
    "mci_clock",
    "sta_after_init",
    "sta_after_cmd0",
    "cmd_readback_cmd0",
    "cmd0_wait_ticks",
    "sta_after_cmd8",
    "cmd_readback_cmd8",
    "cmd8_wait_ticks",
    "resp0_cmd8",
]

def main():
    print("Connecting to device...")
    dev = find_device()
    print(f"Device found: {dev}")

    stub = STUB_PATH.read_bytes()
    print(f"Stub size: {len(stub)} bytes")

    dev.write_mem(STUB_ADDR, stub)
    print("Stub written, executing...")
    dev.execute(STUB_ADDR, wait=True)

    raw = dev.read_mem(RESULT_ADDR, 20 * 4)
    words = struct.unpack("<20I", raw)

    for i, name in enumerate(FIELD_NAMES):
        val = words[i]
        print(f"  [{i:2d}] {name:22s} = 0x{val:08X}")

    # Interpret key results
    magic = words[0]
    if magic != 0x494F4354:
        print(f"\n  BAD MAGIC: 0x{magic:08X} (expected 0x{0x494F4354:08X})")
        return

    print()
    io_before = words[2]
    io_after  = words[10]
    print(f"  I/O control before: 0x{io_before:08X}  after: 0x{io_after:08X}")
    print(f"  Bit0 set by driver: {'YES' if (io_after & 1) and not (io_before & 1) else 'ALREADY SET' if (io_before & 1) else 'NOT SET'}")

    sta_cmd0 = words[13]
    sta_cmd8 = words[16]
    print(f"  STA after CMD0: 0x{sta_cmd0:08X}  (CMD_SENT={bool(sta_cmd0 & 0x20)}, CMD_ACTIVE={bool(sta_cmd0 & 0x200)})")
    print(f"  STA after CMD8: 0x{sta_cmd8:08X}  (RESP_END={bool(sta_cmd8 & 0x10)}, RESP_TIMEO={bool(sta_cmd8 & 0x04)})")
    print(f"  RESP0 after CMD8: 0x{words[19]:08X}")

    t0 = words[15]
    t8 = words[18]
    print(f"  CMD0 wait ticks: {t0}")
    print(f"  CMD8 wait ticks: {t8}")

    if sta_cmd8 & 0x10:  # RESP_END
        print(f"\n  *** RESPONSE RECEIVED! CARD IS ALIVE! ***")
    elif sta_cmd8 & 0x04:  # RESP_TIMEO
        print(f"\n  RESP_TIMEO - card not responding")
    else:
        print(f"\n  No response event - card not detected on bus")

if __name__ == "__main__":
    main()
