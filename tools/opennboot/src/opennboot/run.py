from __future__ import annotations

import sys

from .images import resolve
from .nand import connect

LOAD_ADDR = 0x30000000


def run(addr: int = LOAD_ADDR) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    source = resolve("opennboot.bin")
    payload = source.read_bytes()

    print("Running openNBOOT from memory. The NAND is not modified.")
    print(f"Image: {source} ({len(payload)} bytes)")
    if addr != LOAD_ADDR:
        print(f"Load address: {addr:#010x}.")

    dev = connect()
    print("Device connected.")

    dev.write_mem(addr, payload)
    print(f"Uploaded {len(payload)} bytes.")

    dev.execute(addr, wait=False)
    print("openNBOOT started. The boot ROM will not resume. Power-cycle the "
          "device to return to USB boot mode.")
    print("To read the log afterwards: opennboot log")
    return 0
