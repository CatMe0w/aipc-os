#!/usr/bin/env python3
"""Install openNBOOT into NAND block 0, replacing the bootloader that shipped
with the device."""

from __future__ import annotations

import argparse
import sys
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "tools" / "usbboot" / "src"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "ddr-init" / "src"))

from ak7802_usbboot.transport import find_device
from aipc_ddr_init.cli import ddr_init

from nfblock import (
    BLOCK_PAGES, FIRMWARE, PAGE_DATA, PAGE_RAW, PAYLOAD,
    NandBlock, NandError, after_write, confirm, describe_mismatch,
)

SIGNATURE = b"ANYKA382"
SIG_OFFSET = 0x04
# Bytes 0 and 1 of the header's load descriptor counts word.
CHUNKS_OFFSET = 0x0C
PAGE_COUNT_OFFSET = 0x0D

# One header page plus payload, without spilling out of block 0.
MAX_PAYLOAD_PAGES = BLOCK_PAGES - 1


def build_image(original: bytes, payload: bytes) -> tuple[bytes, int]:
    header = original[:PAGE_DATA]
    if header[SIG_OFFSET:SIG_OFFSET + 8] != SIGNATURE:
        raise NandError(
            "The header page does not carry the ANYKA382 signature. Refusing to "
            "write a block the bootrom would not accept"
        )

    declared = header[CHUNKS_OFFSET] * 512
    if declared != PAGE_DATA:
        raise NandError(
            f"The header declares a {declared} byte page. These tools are "
            f"built around {PAGE_DATA}"
        )

    installed = header[PAGE_COUNT_OFFSET]
    if installed > MAX_PAYLOAD_PAGES:
        raise NandError(
            f"The installed bootloader claims {installed} pages, past the "
            f"{MAX_PAYLOAD_PAGES} this tool reads and saves. It cannot be "
            f"backed up safely"
        )

    page_count = (len(payload) + PAGE_DATA - 1) // PAGE_DATA
    if page_count > MAX_PAYLOAD_PAGES:
        raise NandError(
            f"Payload needs {page_count} pages, more than the "
            f"{MAX_PAYLOAD_PAGES} that fit in one block"
        )

    patched = bytearray(header)
    patched[PAGE_COUNT_OFFSET] = page_count

    image = bytearray(b"\xff" * (BLOCK_PAGES * PAGE_RAW))
    image[0:PAGE_DATA] = patched
    for i in range(page_count):
        off = (i + 1) * PAGE_RAW
        chunk = payload[i * PAGE_DATA:(i + 1) * PAGE_DATA]
        image[off:off + PAGE_DATA] = chunk.ljust(PAGE_DATA, b"\xff")
    return bytes(image), page_count


def save_backup(original: bytes, path: Path) -> None:
    if path.exists():
        raise NandError(f"{path} already exists. Refusing to overwrite it")
    path.write_bytes(original)
    problem = describe_mismatch(path.read_bytes(), original)
    if problem:
        raise NandError(
            f"{path} does not match what was read from the device: {problem}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    # Only useful against a scratch block, so it stays out of --help.
    parser.add_argument("--row", type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args()

    # Progress has to reach the user as it happens, not when the buffer fills:
    # "do not disconnect" is worthless if it appears after the write.
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    if not PAYLOAD.exists():
        print(f"openNBOOT not found, build it first\n", file=sys.stderr)
        return 1
    payload = PAYLOAD.read_bytes()
    backup = Path.cwd() / f"backup_nboot-{datetime.now():%Y%m%d-%H%M%S}.bin"

    print("openNBOOT installer")
    print("This replaces the bootloader your device shipped with. The current "
          "one will be saved first.")
    if args.row:
        print(f"  [using scratch target: row {args.row}]")
    print()

    dev = find_device(wait=True)
    ddr_init(dev, firmware=FIRMWARE)
    print("Device connected.")

    nb = NandBlock(dev)
    nb.check_ddr()

    original = nb.read_pages(args.row)
    # print(f"chip id {nb.last_id:#010x}")
    save_backup(original, backup)
    print(f"Current bootloader saved to {backup.name} ({len(original)} bytes)")
    print()
    print("  [!] Keep this file. It is the only copy of the bootloader your "
          "device shipped with.")
    print(f"  To undo this install:  uv run tools/restore.py --image {backup.name}")
    print()

    image, _ = build_image(original, payload)
    # image, page_count = build_image(original, payload)
    # print(f"Reusing this device's own header page, page count "
    #       f"{original[PAGE_COUNT_OFFSET]} -> {page_count}")
    # print()

    if not confirm("This erases and replaces the bootloader."):
        print("Aborted, nothing was written. The backup above is still intact.")
        return 0

    print("Writing, do not disconnect.")
    written = nb.write_pages(args.row, image)

    problem = describe_mismatch(nb.read_pages(args.row), after_write(image))
    if problem:
        print(f"VERIFY FAILED after writing {written} pages: {problem}",
              file=sys.stderr)
        print("The device will not boot from NAND in this state. Restore the "
              "original bootloader with:", file=sys.stderr)
        print(f"  uv run tools/restore.py --image {backup.name}", file=sys.stderr)
        return 1

    print("Done. openNBOOT is installed successfully.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except NandError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
