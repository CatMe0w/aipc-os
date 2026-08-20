"""Run openNBOOT from memory without an install.

Uploads opennboot.bin to DDR and executes it in place, so it exercises the whole
boot flow with no NAND write. The bootrom does not resume afterwards. The log
survives the next power cycle, and `opennboot log` reads it back.
"""

from __future__ import annotations

import sys

from .images import resolve
from .nand import connect

LOAD_ADDR = 0x30000000


def run(addr: int = LOAD_ADDR) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    source = resolve("opennboot.bin")
    payload = source.read_bytes()

    print("openNBOOT dry run")
    print("This runs openNBOOT from memory. It writes nothing to NAND.")
    print(f"Image: {source} ({len(payload)} bytes)")
    if addr != LOAD_ADDR:
        print(f"[using load address {addr:#010x}]")
    print()

    dev = connect()
    print("Device connected.")

    dev.write_mem(addr, payload)
    print(f"Uploaded openNBOOT ({len(payload)} bytes)")

    dev.execute(addr, wait=False)
    print("Starting openNBOOT. The bootrom will not resume.")
    print()
    print("  To read the log:  opennboot log")
    return 0
