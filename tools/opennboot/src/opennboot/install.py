from __future__ import annotations

import sys
from pathlib import Path

from .images import resolve
from .nand import Block0, NandError, WriteFailed, confirm, connect, id_hex, xxh32


def backup_path(original: bytes, nand_id: int) -> Path:
    return Path.cwd() / f"backup_nboot-{id_hex(nand_id)}-{xxh32(original):08x}.bin"


def save_backup(path: Path, original: bytes) -> None:
    if path.exists() and path.read_bytes() == original:
        return
    path.write_bytes(original)
    if path.read_bytes() != original:
        raise NandError(
            f"{path}: the file does not read back as written. The NAND was "
            f"not modified."
        )


def run(row: int = 0) -> int:
    # With a piped stdout, the "do not disconnect" line would otherwise appear
    # only after the write.
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    source = resolve("opennboot.bin")
    payload = source.read_bytes()

    print("Installing openNBOOT.")
    print(f"Image: {source} ({len(payload)} bytes)")
    if row:
        print(f"Scratch target: row {row}.")

    dev = connect()
    print("Device connected.")

    b0 = Block0(dev)
    original, nand_id = b0.read(row)
    b0.build(payload)

    if not confirm("The bootloader in NAND block 0 will be erased and replaced. "
                   "It will be saved to the current directory first."):
        print("Aborted. The NAND was not modified.")
        return 0

    backup = backup_path(original, nand_id)
    save_backup(backup, original)
    print(f"Current bootloader saved to {backup.name} ({len(original)} bytes).")
    print("Important: keep this file. It is the only copy of the bootloader "
          "that this installation replaces.")
    print(f"To revert the installation: opennboot restore --image {backup.name}")

    print("Writing to NAND. Do not disconnect the device.")
    try:
        b0.write(row)
    except WriteFailed as exc:
        print(f"Error: {exc}", file=sys.stderr)
        print("The device may not boot from NAND in this state. To restore the "
              "original bootloader:", file=sys.stderr)
        print(f"  opennboot restore --image {backup.name}", file=sys.stderr)
        return 1

    print("Installation complete.")
    return 0
