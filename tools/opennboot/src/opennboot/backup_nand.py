from __future__ import annotations

import sys
from pathlib import Path

import tqdm
from lz4.frame import LZ4FrameCompressor

from .nand import BACKUP_MAX_PAGES, PAGE_RAW, NandError, NandReader, connect, id_hex


def run(pages: int | None = None) -> int:
    sys.stdout.reconfigure(line_buffering=True) # pyright: ignore[reportAttributeAccessIssue]

    partial = Path.cwd() / "backup_nand.img.partial"

    print("Reading the entire NAND.")

    dev = connect()
    print("Device connected.")
    reader = NandReader(dev)

    nand_id, total, _ = reader.identify()
    print(f"NAND ID: {id_hex(nand_id)} ({total} pages)")
    if pages is not None:
        total = min(total, pages)
        print(f"Limited to the first {total} pages.")

    lz4 = LZ4FrameCompressor(content_checksum=True)

    with partial.open("wb") as f, tqdm.tqdm(
        total=total * PAGE_RAW, unit="B", unit_scale=True, unit_divisor=1024,
        desc="reading", leave=True,
    ) as bar:
        f.write(lz4.begin())
        for row in range(0, total, BACKUP_MAX_PAGES):
            count = min(BACKUP_MAX_PAGES, total - row)
            data = reader.read_pages(row, count, progress=bar.update)
            f.write(lz4.compress(data))
        # The frame ends with the xxHash32 of the image, little-endian
        end = lz4.flush()
        f.write(end)

    checksum = int.from_bytes(end[-4:], "little")
    image = partial.with_name(
        f"backup_nand-{id_hex(nand_id)}-{checksum:08x}.img.lz4")
    if image.exists():
        raise NandError(
            f"{image}: file exists. The backup was kept as {partial.name}"
        )
    partial.rename(image)

    print(f"Backup written to {image.name} ({total * PAGE_RAW} bytes uncompressed).")
    return 0
