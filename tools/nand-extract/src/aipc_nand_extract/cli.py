from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

import click

PAGE_SIZE = 0x800
CHUNK_SIZE = 0x200
OOB_SIZE = 0x10
RAW_CHUNK_SIZE = CHUNK_SIZE + OOB_SIZE
CHUNKS_PER_PAGE = PAGE_SIZE // CHUNK_SIZE
RAW_PAGE_SIZE = RAW_CHUNK_SIZE * CHUNKS_PER_PAGE
PTB_PAYLOAD_SIZE = 0x7F4
PTB_ENTRY_SIZE = 0x30
NBOOT_CODE_OFFSET = PAGE_SIZE
NBOOT_CODE_SIZE = PAGE_SIZE * 2
NBOOT_CODE_LOAD_BASE = 0x30000000
IMG_HEADER_SIZE = 0x2C
EBOOT_VIEW_SIZE = 0x64000
CHILD_PARTITION_TABLE_OFFSET = 0x1BE
CHILD_PARTITION_ENTRY_SIZE = 0x10
CHILD_PARTITION_COUNT = 4
PARTITION_TYPE_BINFS = 0x21
PARTITION_TYPE_FAT = 0x04
CHAIN_INFO = b"@chain information"
PTB_ENTRY_TAGS = (
    b"NBT\x00",
    b"IPL\x00",
    b"BAK\x00",
    b"UDR\x00",
    b"NK\x00\x00",
    b"DSK\x00",
    b"CFG\x00",
    b"END\x00",
)


@dataclass(frozen=True)
class NANDGeometry:
    raw_size: int
    clean_size: int
    total_pages: int
    total_blocks: int
    block_size: int
    pages_per_block: int

    @property
    def raw_block_size(self) -> int:
        return self.pages_per_block * RAW_PAGE_SIZE

    def to_json(self) -> dict:
        return {
            "page_size": PAGE_SIZE,
            "raw_page_size": RAW_PAGE_SIZE,
            "chunk_size": CHUNK_SIZE,
            "oob_size": OOB_SIZE,
            "chunks_per_page": CHUNKS_PER_PAGE,
            "clean_size": self.clean_size,
            "raw_size": self.raw_size,
            "total_pages": self.total_pages,
            "block_size": self.block_size,
            "raw_block_size": self.raw_block_size,
            "pages_per_block": self.pages_per_block,
            "total_blocks": self.total_blocks,
        }


@dataclass(frozen=True)
class PTBEntry:
    index: int
    raw_tag: bytes
    filename: str
    unk0: int
    flags: int
    start_block: int
    block_count: int
    load_addr: int

    @property
    def tag(self) -> str:
        return self.raw_tag.rstrip(b"\x00").decode("ascii", errors="replace")

    def offset(self, geometry: NANDGeometry) -> int:
        return self.start_block * geometry.block_size

    def size(self, geometry: NANDGeometry) -> int:
        return self.block_count * geometry.block_size

    def to_json(self, geometry: NANDGeometry) -> dict:
        return {
            "index": self.index,
            "tag": self.tag,
            "filename": self.filename,
            "unk0": self.unk0,
            "flags": self.flags,
            "start_block": self.start_block,
            "block_count": self.block_count,
            "offset": self.offset(geometry),
            "size": self.size(geometry),
            "load_addr": self.load_addr,
        }


@dataclass(frozen=True)
class PTBCandidate:
    page_index: int
    clean_offset: int
    raw_offset: int
    ptb_raw: bytes
    save_sector: int
    save_count: int
    table_offset: int
    entries: list[PTBEntry]
    geometry: NANDGeometry


@dataclass(frozen=True)
class ChildPartition:
    index: int
    boot_indicator: int
    partition_type: int
    lba_start: int
    sector_count: int

    @property
    def offset(self) -> int:
        return self.lba_start * PAGE_SIZE

    @property
    def size(self) -> int:
        return self.sector_count * PAGE_SIZE

    def to_json(self) -> dict:
        return {
            "index": self.index,
            "boot_indicator": self.boot_indicator,
            "partition_type": self.partition_type,
            "lba_start": self.lba_start,
            "sector_count": self.sector_count,
            "offset": self.offset,
            "size": self.size,
        }


@dataclass(frozen=True)
class ECECImage:
    index: int
    offset: int
    span_size: int
    load_base: int
    header_field_44: int
    header_field_48: int

    def to_json(self) -> dict:
        return {
            "index": self.index,
            "offset": self.offset,
            "span_size": self.span_size,
            "load_base": self.load_base,
            "header_field_44": self.header_field_44,
            "header_field_48": self.header_field_48,
        }


def decode_c_string(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def normalize_plain(page: bytes) -> bytes:
    return page[:PAGE_SIZE]


def normalize_interleaved(page: bytes) -> bytes:
    return b"".join(page[i * RAW_CHUNK_SIZE : i * RAW_CHUNK_SIZE + CHUNK_SIZE] for i in range(CHUNKS_PER_PAGE))


def normalize_nbt_page(page: bytes) -> bytes:
    if page[PAGE_SIZE:] == b"\xFF" * (RAW_PAGE_SIZE - PAGE_SIZE):
        return normalize_plain(page)
    return normalize_interleaved(page)


def parse_ptb_entry(raw: bytes, index: int) -> PTBEntry:
    return PTBEntry(
        index=index,
        raw_tag=raw[4:8],
        filename=decode_c_string(raw[8:24]),
        unk0=struct.unpack_from("<I", raw, 0x00)[0],
        flags=struct.unpack_from("<I", raw, 0x1C)[0],
        start_block=struct.unpack_from("<I", raw, 0x20)[0],
        block_count=struct.unpack_from("<I", raw, 0x24)[0],
        load_addr=struct.unpack_from("<I", raw, 0x28)[0],
    )


def is_valid_ptb_entries(entries: list[PTBEntry]) -> bool:
    if len(entries) != len(PTB_ENTRY_TAGS):
        return False
    for index, entry in enumerate(entries):
        if entry.raw_tag != PTB_ENTRY_TAGS[index]:
            return False
        if entry.block_count == 0:
            return False
    if entries[0].start_block != 0:
        return False
    for current, next_entry in zip(entries, entries[1:]):
        if next_entry.start_block < current.start_block + current.block_count:
            return False
    return True


def parse_ptb_table(ptb_raw: bytes) -> tuple[int, list[PTBEntry]]:
    matches: list[tuple[int, list[PTBEntry]]] = []
    table_size = len(PTB_ENTRY_TAGS) * PTB_ENTRY_SIZE

    for table_offset in range(0, len(ptb_raw) - table_size + 1, 4):
        tags_match = all(
            ptb_raw[table_offset + index * PTB_ENTRY_SIZE + 4 : table_offset + index * PTB_ENTRY_SIZE + 8] == tag
            for index, tag in enumerate(PTB_ENTRY_TAGS)
        )
        if not tags_match:
            continue

        entries = [
            parse_ptb_entry(
                ptb_raw[table_offset + index * PTB_ENTRY_SIZE : table_offset + (index + 1) * PTB_ENTRY_SIZE],
                index,
            )
            for index in range(len(PTB_ENTRY_TAGS))
        ]
        if is_valid_ptb_entries(entries):
            matches.append((table_offset, entries))

    if not matches:
        raise ValueError("PTB entry table not found")
    if len(matches) > 1:
        offsets = ", ".join(f"0x{offset:X}" for offset, _ in matches)
        raise ValueError(f"ambiguous PTB entry tables: {offsets}")
    return matches[0]


def derive_geometry(raw_size: int, total_pages: int, entries: list[PTBEntry]) -> NANDGeometry:
    end_entry = entries[-1]
    total_blocks = end_entry.start_block + end_entry.block_count
    clean_size = total_pages * PAGE_SIZE

    if total_blocks <= 0:
        raise ValueError("PTB END entry gives zero total block count")
    if clean_size % total_blocks:
        raise ValueError("clean image size is not divisible by PTB block count")

    block_size = clean_size // total_blocks
    if block_size % PAGE_SIZE:
        raise ValueError("derived block size is not page-aligned")

    pages_per_block = block_size // PAGE_SIZE
    if pages_per_block <= 0 or pages_per_block * total_blocks != total_pages:
        raise ValueError("derived block geometry does not match raw page count")

    geometry = NANDGeometry(
        raw_size=raw_size,
        clean_size=clean_size,
        total_pages=total_pages,
        total_blocks=total_blocks,
        block_size=block_size,
        pages_per_block=pages_per_block,
    )

    for entry in entries:
        if entry.start_block + entry.block_count > geometry.total_blocks:
            raise ValueError(f"PTB entry {entry.tag} exceeds derived NAND geometry")

    return geometry


def parse_ptb_candidate(page_index: int, page: bytes, raw_size: int, total_pages: int) -> PTBCandidate:
    clean = normalize_interleaved(page)
    ptb_raw = clean[:PTB_PAYLOAD_SIZE]
    if ptb_raw[:4] != b"PTB\x00":
        raise ValueError("PTB magic not found")
    if ptb_raw[4:8] != b"01\x00\x00":
        raise ValueError("unsupported PTB version")

    save_sector, save_count = struct.unpack_from("<II", ptb_raw, 0x08)
    if save_sector != page_index:
        raise ValueError("PTB save_sector does not match page index")

    table_offset, entries = parse_ptb_table(ptb_raw)
    geometry = derive_geometry(raw_size, total_pages, entries)

    return PTBCandidate(
        page_index=page_index,
        clean_offset=page_index * PAGE_SIZE,
        raw_offset=page_index * RAW_PAGE_SIZE,
        ptb_raw=ptb_raw,
        save_sector=save_sector,
        save_count=save_count,
        table_offset=table_offset,
        entries=entries,
        geometry=geometry,
    )


def scan_ptb(raw_path: Path, raw_size: int, total_pages: int) -> PTBCandidate:
    candidates: list[PTBCandidate] = []
    with raw_path.open("rb") as f:
        for page_index in range(total_pages):
            page = f.read(RAW_PAGE_SIZE)
            if len(page) != RAW_PAGE_SIZE:
                raise click.ClickException(f"short read at raw page {page_index}")
            if not normalize_interleaved(page).startswith(b"PTB\x00"):
                continue
            try:
                candidate = parse_ptb_candidate(page_index, page, raw_size, total_pages)
            except ValueError:
                continue
            candidates.append(candidate)

    if not candidates:
        raise click.ClickException("PTB not found")
    return max(candidates, key=lambda candidate: (candidate.save_count, candidate.page_index))


def find_entry(entries: list[PTBEntry], tag: str) -> PTBEntry:
    for entry in entries:
        if entry.tag == tag:
            return entry
    raise click.ClickException(f"PTB entry not found: {tag}")


def normalize_raw_nand(raw_path: Path, clean_path: Path, candidate: PTBCandidate) -> None:
    nbt_entry = find_entry(candidate.entries, "NBT")
    nbt_start_page = nbt_entry.start_block * candidate.geometry.pages_per_block
    nbt_end_page = (nbt_entry.start_block + nbt_entry.block_count) * candidate.geometry.pages_per_block

    with raw_path.open("rb") as fi, clean_path.open("wb") as fo:
        for page_index in range(candidate.geometry.total_pages):
            page = fi.read(RAW_PAGE_SIZE)
            if len(page) != RAW_PAGE_SIZE:
                raise click.ClickException(f"short read at raw page {page_index}")
            if nbt_start_page <= page_index < nbt_end_page:
                fo.write(normalize_nbt_page(page))
            else:
                fo.write(normalize_interleaved(page))


def copy_partition(clean_path: Path, out_dir: Path, entry: PTBEntry, geometry: NANDGeometry) -> None:
    out_path = out_dir / f"{entry.tag}.raw"
    with clean_path.open("rb") as fi, out_path.open("wb") as fo:
        fi.seek(entry.offset(geometry))
        remaining = entry.size(geometry)
        while remaining:
            chunk = fi.read(min(1024 * 1024, remaining))
            if not chunk:
                raise click.ClickException(f"short read while extracting {entry.tag}")
            fo.write(chunk)
            remaining -= len(chunk)


def write_slice(src_path: Path, out_path: Path, offset: int, size: int) -> None:
    with src_path.open("rb") as fi, out_path.open("wb") as fo:
        fi.seek(offset)
        remaining = size
        while remaining:
            chunk = fi.read(min(1024 * 1024, remaining))
            if not chunk:
                raise click.ClickException(f"short read while writing {out_path.name}")
            fo.write(chunk)
            remaining -= len(chunk)


def parse_img_header(raw: bytes) -> dict | None:
    if len(raw) < IMG_HEADER_SIZE or raw[:4] != b"IMG\x00":
        return None
    return {
        "type": decode_c_string(raw[4:8]),
        "filename": decode_c_string(raw[8:24]),
        "field_18": struct.unpack_from("<I", raw, 0x18)[0],
        "region_size": struct.unpack_from("<I", raw, 0x1C)[0],
        "load_addr": struct.unpack_from("<I", raw, 0x20)[0],
        "field_24": struct.unpack_from("<I", raw, 0x24)[0],
        "field_28": struct.unpack_from("<I", raw, 0x28)[0],
    }


def parse_child_partitions(raw: bytes) -> list[ChildPartition]:
    if len(raw) < PAGE_SIZE or raw[0x1FE:0x200] != b"\x55\xAA":
        return []

    partitions: list[ChildPartition] = []
    for index in range(CHILD_PARTITION_COUNT):
        off = CHILD_PARTITION_TABLE_OFFSET + index * CHILD_PARTITION_ENTRY_SIZE
        entry = raw[off : off + CHILD_PARTITION_ENTRY_SIZE]
        partition_type = entry[4]
        lba_start = struct.unpack_from("<I", entry, 0x08)[0]
        sector_count = struct.unpack_from("<I", entry, 0x0C)[0]
        if partition_type == 0 or sector_count == 0:
            continue
        partitions.append(
            ChildPartition(
                index=index,
                boot_indicator=entry[0],
                partition_type=partition_type,
                lba_start=lba_start,
                sector_count=sector_count,
            )
        )
    return partitions


def find_u32(raw: bytes, value: int) -> list[int]:
    needle = struct.pack("<I", value)
    hits: list[int] = []
    pos = -4
    while True:
        pos = raw.find(needle, pos + 4)
        if pos < 0:
            return hits
        if pos % 4 == 0:
            hits.append(pos)


def scan_ecec_headers(raw: bytes) -> list[dict]:
    headers: list[dict] = []
    for offset in range(0, len(raw) - 0x4C, PAGE_SIZE):
        if raw[offset + 0x40 : offset + 0x44] != b"ECEC":
            continue
        field_44 = struct.unpack_from("<I", raw, offset + 0x44)[0]
        field_48 = struct.unpack_from("<I", raw, offset + 0x48)[0]
        if field_44 <= field_48:
            continue
        headers.append(
            {
                "offset": offset,
                "field_44": field_44,
                "field_48": field_48,
                "load_base": field_44 - field_48,
            }
        )
    return headers


def scan_chain_spans(raw: bytes, headers: list[dict]) -> dict[int, int]:
    if len(headers) < 2:
        return {}

    first_blob = raw[: headers[1]["offset"]]
    chain_off = first_blob.find(CHAIN_INFO)
    if chain_off < 0:
        return {}

    bases = {header["load_base"] for header in headers}
    spans: dict[int, int] = {}
    for base in bases:
        for hit in find_u32(first_blob, base):
            if not (chain_off - 0x200 <= hit <= chain_off + 0x200):
                continue
            start = max(0, hit - 4)
            end = min(len(first_blob), start + 0x80)
            for rec_off in range(start, max(start, end - 0x20) + 1, 0x20):
                vals = struct.unpack_from("<8I", first_blob, rec_off)
                for tuple_off in (0, 4):
                    load_base = vals[tuple_off + 1]
                    span_size = vals[tuple_off + 2]
                    if load_base in bases and span_size and span_size % PAGE_SIZE == 0:
                        spans[load_base] = span_size
            if spans:
                return spans
    return spans


def find_ecec_images(raw: bytes) -> list[ECECImage]:
    headers = scan_ecec_headers(raw)
    spans = scan_chain_spans(raw, headers)
    images: list[ECECImage] = []
    for index, header in enumerate(headers):
        next_offset = headers[index + 1]["offset"] if index + 1 < len(headers) else len(raw)
        span_size = spans.get(header["load_base"], next_offset - header["offset"])
        images.append(
            ECECImage(
                index=index,
                offset=header["offset"],
                span_size=min(span_size, len(raw) - header["offset"]),
                load_base=header["load_base"],
                header_field_44=header["field_44"],
                header_field_48=header["field_48"],
            )
        )
    return images


def write_analysis_views(out_dir: Path) -> list[dict]:
    views: list[dict] = []

    nbt_path = out_dir / "NBT.raw"
    if nbt_path.exists():
        nbt_size = nbt_path.stat().st_size
        if nbt_size >= NBOOT_CODE_OFFSET + NBOOT_CODE_SIZE:
            path = out_dir / "NBT.code.bin"
            write_slice(nbt_path, path, NBOOT_CODE_OFFSET, NBOOT_CODE_SIZE)
            views.append(
                {
                    "path": path.name,
                    "kind": "nboot_code",
                    "source": nbt_path.name,
                    "source_offset": NBOOT_CODE_OFFSET,
                    "size": NBOOT_CODE_SIZE,
                    "load_base": NBOOT_CODE_LOAD_BASE,
                }
            )

    for tag in ("IPL", "BAK"):
        part_path = out_dir / f"{tag}.raw"
        if not part_path.exists():
            continue
        header = parse_img_header(part_path.read_bytes()[:IMG_HEADER_SIZE])
        if header is None:
            continue
        size = min(EBOOT_VIEW_SIZE, part_path.stat().st_size - IMG_HEADER_SIZE)
        path = out_dir / f"{tag}.eboot.bin"
        write_slice(part_path, path, IMG_HEADER_SIZE, size)
        views.append(
            {
                "path": path.name,
                "kind": "eboot_payload",
                "source": part_path.name,
                "source_offset": IMG_HEADER_SIZE,
                "size": size,
                "load_base": header["load_addr"],
                "img": header,
            }
        )

    nk_path = out_dir / "NK.raw"
    if not nk_path.exists():
        return views

    nk = nk_path.read_bytes()
    child_partitions = parse_child_partitions(nk)
    binfs_path: Path | None = None
    for child in child_partitions:
        if child.offset >= len(nk):
            continue
        size = min(child.size, len(nk) - child.offset)
        if child.partition_type == PARTITION_TYPE_BINFS:
            path = out_dir / "NK.binfs.raw"
            kind = "nk_binfs_partition"
            binfs_path = path
        elif child.partition_type == PARTITION_TYPE_FAT:
            path = out_dir / "NK.fat.raw"
            kind = "nk_fat_partition"
        else:
            continue
        write_slice(nk_path, path, child.offset, size)
        views.append(
            {
                "path": path.name,
                "kind": kind,
                "source": nk_path.name,
                "source_offset": child.offset,
                "size": size,
                "child_partition": child.to_json(),
            }
        )

    if binfs_path is None:
        return views

    binfs = binfs_path.read_bytes()
    for image in find_ecec_images(binfs):
        path = out_dir / f"NK.ecec_{image.index:02d}.raw"
        write_slice(binfs_path, path, image.offset, image.span_size)
        views.append(
            {
                "path": path.name,
                "kind": "nk_ecec_image",
                "source": binfs_path.name,
                "source_offset": image.offset,
                "size": image.span_size,
                "load_base": image.load_base,
                "ecec": image.to_json(),
            }
        )

    return views


def write_metadata(raw_path: Path, out_dir: Path, candidate: PTBCandidate, views: list[dict]) -> None:
    metadata = {
        "input": str(raw_path),
        "geometry": candidate.geometry.to_json(),
        "ptb": {
            "offset": candidate.clean_offset,
            "raw_offset": candidate.raw_offset,
            "page_index": candidate.page_index,
            "payload_size": PTB_PAYLOAD_SIZE,
            "version": decode_c_string(candidate.ptb_raw[4:8]),
            "save_sector": candidate.save_sector,
            "save_count": candidate.save_count,
            "table_offset": candidate.table_offset,
            "raw_path": "ptb.raw",
            "entries": [entry.to_json(candidate.geometry) for entry in candidate.entries],
        },
        "views": views,
    }
    (out_dir / "nand_extract.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")


@click.command()
@click.argument("raw_nand_image", type=click.Path(exists=True))
@click.argument("output_dir", required=False, type=click.Path())
def main(raw_nand_image: str, output_dir: str | None) -> None:
    raw_path = Path(raw_nand_image)
    out_dir = Path(output_dir) if output_dir else raw_path.parent / "nand_extracted"
    out_dir.mkdir(parents=True, exist_ok=True)

    raw_size = raw_path.stat().st_size
    if raw_size % RAW_PAGE_SIZE:
        raise click.ClickException(f"raw NAND size is not a multiple of {RAW_PAGE_SIZE}: {raw_size}")
    total_pages = raw_size // RAW_PAGE_SIZE

    click.echo(f"Scanning PTB in {raw_path}...")
    candidate = scan_ptb(raw_path, raw_size, total_pages)
    (out_dir / "ptb.raw").write_bytes(candidate.ptb_raw)
    click.echo(f"PTB: clean=0x{candidate.clean_offset:08X}, raw=0x{candidate.raw_offset:08X}")
    click.echo(
        f"Geometry: block=0x{candidate.geometry.block_size:X}, "
        f"blocks={candidate.geometry.total_blocks}, pages/block={candidate.geometry.pages_per_block}"
    )

    clean_path = out_dir / "nand.clean.bin"
    click.echo(f"Normalizing -> {clean_path}")
    normalize_raw_nand(raw_path, clean_path, candidate)

    click.echo("Splitting PTB partitions...")
    for entry in candidate.entries:
        copy_partition(clean_path, out_dir, entry, candidate.geometry)
        click.echo(f"  -> {entry.tag}.raw")

    click.echo("Writing analysis views...")
    views = write_analysis_views(out_dir)
    for view in views:
        click.echo(f"  -> {view['path']}")

    write_metadata(raw_path, out_dir, candidate, views)
    click.echo("  -> nand_extract.json")


if __name__ == "__main__":
    main()
