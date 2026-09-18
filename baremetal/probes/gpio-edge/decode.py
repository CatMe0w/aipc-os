import struct
import sys
from pathlib import Path


STATUS = {
    0: "running or incomplete",
    1: "timed out waiting for the SD card to be present",
    2: "timed out waiting for SD card removal",
    3: "timed out waiting for SD card insertion",
    4: "timed out waiting for initial key release",
    5: "timed out waiting for key press",
    6: "timed out waiting for final key release",
    7: "complete",
}

SNAPSHOT_NAMES = (
    "initial",
    "falling armed",
    "falling immediate",
    "falling settled",
    "rising armed",
    "rising immediate",
    "rising settled",
    "restored",
)


def main() -> None:
    data = Path(sys.argv[1]).read_bytes()
    if len(data) != 364:
        raise SystemExit(f"bad result size: {len(data)}, expected 364")
    words = struct.unpack_from("<" + "I" * (len(data) // 4), data)

    if words[0] != 0x45475041:
        raise SystemExit(f"bad magic: 0x{words[0]:08x}")

    print(f"version: {words[1]}")
    print(f"status: {STATUS.get(words[2], f'unknown {words[2]}')}")
    print(
        f"control falling: input=0x{words[8]:08x} "
        f"status=0x{words[9]:08x} int=0x{words[14]:08x} "
        f"expected=0x{words[7]:08x}"
    )
    print(
        f"control rising:  input=0x{words[10]:08x} "
        f"status=0x{words[11]:08x} int=0x{words[15]:08x} "
        f"expected=0x{words[7]:08x}"
    )
    print(f"falling WGPIO bits: 0x{words[12]:08x}")
    print(f"rising WGPIO bits:  0x{words[13]:08x}")

    # Version 1 stored the GPIO status bit in this field by mistake. The
    # observed parent interrupt is SYSCTRL bit 27.
    sysctrl_parent_mask = (1 << 27) if words[1] == 1 else words[4]
    wgpio_parent_mask = words[5]
    offset = 27
    for name in SNAPSHOT_NAMES:
        snap = words[offset : offset + 8]
        key = 1 if snap[0] & (1 << 3) else 0
        sysctrl_parent = 1 if snap[1] & sysctrl_parent_mask else 0
        wgpio_parent = 1 if snap[1] & wgpio_parent_mask else 0
        print(
            f"{name:18s} key={key} sysctrl_parent={sysctrl_parent} "
            f"wgpio_parent={wgpio_parent} int=0x{snap[1]:08x} "
            f"gen=0x{snap[2]:08x}/0x{snap[3]:08x} "
            f"wake=0x{snap[5]:08x}/0x{snap[4]:08x}/0x{snap[6]:08x}"
        )
        offset += 8


if __name__ == "__main__":
    main()
