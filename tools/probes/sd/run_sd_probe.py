import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "usbboot" / "src"))
from ak7802_usbboot.transport import find_device

STUB_ADDR   = 0x48000240
RESULT_ADDR = 0x48001100
STUB_DIR    = Path(__file__).resolve().parent / "stub"

PROBES = [
    ("sd_clock_probe",     0x4D4D434B, "MCK"),
    ("sd_sharepin_probe",  0x4D4D5350, "MSP"),
    ("sd_reg_probe",       0x4D4D5247, "MRG"),
    ("sd_cd_probe",        0x4D4D4344, "MCD"),
    ("sd_cmd0_probe",      0x4D4D4330, "MC0"),
]


def run_one(dev, name, expected_magic):
    stub_path = STUB_DIR / f"{name}.bin"
    if not stub_path.exists():
        print(f"  SKIP: {stub_path} not found (run 'make' in stub/)")
        return None

    stub = stub_path.read_bytes()
    print(f"  stub: {len(stub)} bytes")

    dev.write_mem(STUB_ADDR, stub)
    dev.execute(STUB_ADDR, wait=True)

    raw = dev.read_mem(RESULT_ADDR, 8 * 4)
    words = struct.unpack("<8I", raw)

    magic = words[0]
    if magic != expected_magic:
        print(f"  BAD MAGIC: 0x{magic:08X} (expected 0x{expected_magic:08X})")
        return None

    return words


def main():
    print("Connecting...")
    dev = find_device()

    for name, magic, label in PROBES:
        print(f"\n=== {label} ({name}) ===")
        w = run_one(dev, name, magic)
        if w is None:
            print(f"  => HUNG or FAILED at {label}")

            # Try to read what we can
            try:
                import time
                time.sleep(0.5)
                dev2 = find_device()
                raw = dev2.read_mem(RESULT_ADDR, 32 * 4)
                dump = struct.unpack("<32I", raw)
                print(f"  Raw result dump (32 words):")
                for i in range(0, 32, 8):
                    vals = " ".join(f"0x{v:08X}" for v in dump[i:i+8])
                    print(f"    [{i:2d}] {vals}")
            except Exception as e:
                print(f"  (could not read residual: {e})")
            break

        print(f"  magic = 0x{w[0]:08X}")
        for i in range(2, 8):
            print(f"  [{i}] = 0x{w[i]:08X}")


if __name__ == "__main__":
    main()
