from __future__ import annotations

import re
import sys
from pathlib import Path

import tqdm
from lz4.frame import LZ4FrameDecompressor, get_frame_info

from .nand import (
    BACKUP_MAX_PAGES, PAGE_RAW,
    NandError, NandReader, NandWriter, confirm, connect, id_hex,
)

LZ4_MAGIC = b"\x04\x22\x4d\x18"
BLANK_PAGE = b"\xff" * PAGE_RAW
# Physical column of the bad block marker in the first page of a block.
# See docs/nboot/boot-flow.md.
BAD_MARKER_COL = 0x411


def load(path: Path) -> tuple[bytes, int | None, bool]:
    data = path.read_bytes()
    if data[:4] != LZ4_MAGIC:
        return data, None, True

    lz4 = LZ4FrameDecompressor()
    if not get_frame_info(data)["content_checksum"]:
        return lz4.decompress(data), None, True

    image = lz4.decompress(data[:-4])
    try:
        lz4.decompress(data[-4:])
        ok = True
    except RuntimeError:
        ok = False
    return image, int.from_bytes(data[-4:], "little"), ok


def warn(text: str) -> bool:
    print(f"Warning: {text}")
    try:
        return input("Proceed anyway? [y/N] ").strip().lower() == "y"
    except (EOFError, KeyboardInterrupt):
        print()
        return False


def run(path: Path, first_block: int = 0, blocks: int | None = None) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    print(f"Image: {path}")

    image, checksum, checksum_ok = load(path)
    aborted = "Aborted. The NAND was not modified."

    if not checksum_ok:
        if not warn(f"content checksum mismatch (stored checksum: {checksum:08x}). "
                    f"The file is corrupted, or it was modified without "
                    f"updating the checksum."):
            print(aborted)
            return 0
    if checksum is not None and not re.search(rf"\b{checksum:08x}\b", path.name):
        if not warn(f"the file name does not contain the stored checksum "
                    f"{checksum:08x}. The file was renamed, or it was modified "
                    f"and recompressed."):
            print(aborted)
            return 0

    dev = connect()
    print("Device connected.")

    nand_id, pages, ppb = NandReader(dev).identify()
    print(f"NAND ID: {id_hex(nand_id)} ({pages} pages, "
          f"{ppb} pages per block)")
    if len(image) != pages * PAGE_RAW:
        raise NandError(
            f"Image size mismatch: {len(image)} bytes, expected "
            f"{pages * PAGE_RAW} bytes ({pages} pages of {PAGE_RAW} bytes)"
        )

    row = first_block * ppb
    end = pages if blocks is None else min(pages, row + blocks * ppb)
    if first_block or blocks:
        print(f"Limited to pages {row}-{end - 1}.")

    writer = NandWriter(dev, ppb)
    batch = BACKUP_MAX_PAGES

    bad: list[int] = []
    for at in range(row, end, batch):
        bad += writer.run(writer.SCAN, at, min(batch, end - at))
    print(f"Bad blocks on device: {', '.join(map(str, bad)) if bad else 'none'}")
    lost = [b for b in bad
            if image[b * ppb * PAGE_RAW + BAD_MARKER_COL] == 0xFF
            and image[b * ppb * PAGE_RAW:(b + 1) * ppb * PAGE_RAW] != BLANK_PAGE * ppb]
    if lost and not warn(f"the image contains data in blocks that are marked bad "
                         f"on this device: {', '.join(map(str, lost))}. Bad blocks "
                         f"are neither erased nor written. This part of the data will "
                         f"not be restored."):
        print(aborted)
        return 0

    if not confirm("All data on the NAND will be erased and replaced with the image."):
        print(aborted)
        return 0

    print("Writing to NAND. Do not disconnect the device.")
    with tqdm.tqdm(
        total=(end - row) * PAGE_RAW, unit="B", unit_scale=True, unit_divisor=1024,
        desc="writing", leave=True,
    ) as bar:
        for at in range(row, end, batch):
            count = min(batch, end - at)
            data = image[at * PAGE_RAW:(at + count) * PAGE_RAW]
            if data == BLANK_PAGE * count:
                writer.run(writer.ERASE, at, count)
                bar.update(len(data))
            else:
                writer.run(writer.WRITE, at, count, data, progress=bar.update)

    print("Restore complete.")
    return 0
