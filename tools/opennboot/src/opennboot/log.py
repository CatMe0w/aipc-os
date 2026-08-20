"""Read back the logs the bare-metal images leave in memory.

DDR keeps its contents across a power cycle, so this recovers a log from the
previous run once the device is back in USB boot mode.

The window layout is the shared log pool in baremetal/README.md. The table
below must match it.
"""

from __future__ import annotations

import sys
from pathlib import Path

from .nand import connect

LOG_SIZE = 0x00010000

# Slot name -> the windows it covers, oldest first. gdbstub keeps two, because
# it moves its live log aside at every startup. After a crash, the run you want
# is the previous one.
SLOTS: dict[str, list[tuple[str, int]]] = {
    "opennboot": [("openNBOOT", 0x301F0000)],
    "bootbin": [("bootbin", 0x301E0000)],
    "gdbstub": [
        ("gdbstub trace, previous run", 0x301D0000),
        ("gdbstub trace, current run", 0x301C0000),
    ],
    "aipc-boot": [("aipc-boot", 0x301B0000)],
    "doom": [("DOOM", 0x301A0000)],
}

DEFAULT_SLOT = "opennboot"


def windows_for(slot: str, base: int | None, every: bool) -> list[tuple[str, int]]:
    """The windows to read, as (title, address), in the order to print them."""
    if base is not None:
        return [(f"{base:#010x}", base)]
    if every:
        return [w for name in SLOTS for w in SLOTS[name]]
    return SLOTS[slot]


def _show(title: str, data: bytes) -> None:
    text = data.rstrip(b"\x00")

    print()
    print(f"--- {title} ---")
    if not text:
        print("(empty)")
        return
    print(text.decode("ascii", errors="replace"), end="")
    if not text.endswith(b"\n"):
        print()


def run(windows: list[tuple[str, int]], out: Path | None = None) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    dev = connect()
    print("Device connected.")

    for title, addr in windows:
        data = dev.read_mem(addr, LOG_SIZE)
        if out:
            out.write_bytes(data)
            print(f"Raw buffer saved to {out.name} ({len(data)} bytes)")
        _show(title, data)

    return 0
