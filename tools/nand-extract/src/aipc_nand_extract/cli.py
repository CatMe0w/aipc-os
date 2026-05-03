from __future__ import annotations

import json
import math
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path

import bchlib
import click

from .cecompress import CECOMPRESS_FLAG, CECompressError, maybe_decompress_cecompress

PAGE_SIZE = 0x800
CHUNK_SIZE = 0x200
OOB_SIZE = 0x10
RAW_CHUNK_SIZE = CHUNK_SIZE + OOB_SIZE
CHUNKS_PER_PAGE = PAGE_SIZE // CHUNK_SIZE
RAW_PAGE_SIZE = RAW_CHUNK_SIZE * CHUNKS_PER_PAGE
MAX_ECEC_LOGICAL_PAGES_PER_RAW_GROUP = 512
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
CHAIN_INFO_NAME = b"chain information\x00"
ROMHDR_SIZE = 0x54
ROM_MODULE_ENTRY_SIZE = 0x20
ROM_FILE_ENTRY_SIZE = 0x1C
E32_ROM_SIZE = 0x70
O32_ROM_SIZE = 0x18
PE_DOS_HEADER_SIZE = 0x80
PE_OPTIONAL_HEADER_SIZE = 0xE0
PE_FILE_ALIGNMENT = 0x200
PE_SECTION_ALIGNMENT = 0x1000
PE_MACHINE_ARM = 0x1C2
ARM_CPU_TYPE = 0x1C2
MAX_ROM_MODULES = 4096
MAX_ROM_FILES = 65536
MAX_E32_OBJECTS = 16
MAX_EXPORT_NAMES = 512
EXPORT_SCAN_WINDOW = 0x2000
MODULE_NAME_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}\.(?:dll|DLL|exe|EXE|drv|DRV|cpl|CPL)$")
MODULE_NAME_BYTES_RE = re.compile(rb"[A-Za-z0-9_.-]{1,64}\.(?:dll|DLL|exe|EXE|drv|DRV|cpl|CPL)\x00")
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
    logical_size: int
    logical_pages_per_raw_group: int
    metadata_page_index: int
    load_base: int
    header_field_44: int
    header_field_48: int

    def to_json(self) -> dict:
        return {
            "index": self.index,
            "offset": self.offset,
            "span_size": self.span_size,
            "logical_size": self.logical_size,
            "logical_pages_per_raw_group": self.logical_pages_per_raw_group,
            "metadata_page_index": self.metadata_page_index,
            "load_base": self.load_base,
            "header_field_44": self.header_field_44,
            "header_field_48": self.header_field_48,
        }


@dataclass(frozen=True)
class ROMHDR:
    offset: int
    fields: tuple[int, ...]

    @property
    def physfirst(self) -> int:
        return self.fields[2]

    @property
    def physlast(self) -> int:
        return self.fields[3]

    @property
    def rom_size(self) -> int:
        return self.physlast - self.physfirst

    @property
    def nummods(self) -> int:
        return self.fields[4]

    @property
    def numfiles(self) -> int:
        return self.fields[12]

    @property
    def cpu_type(self) -> int:
        return self.fields[17] & 0xFFFF

    @property
    def misc_flags(self) -> int:
        return self.fields[17] >> 16

    @property
    def module_table_offset(self) -> int:
        return self.offset + ROMHDR_SIZE

    @property
    def file_table_offset(self) -> int:
        return self.module_table_offset + self.nummods * ROM_MODULE_ENTRY_SIZE

    @property
    def table_end_offset(self) -> int:
        return self.file_table_offset + self.numfiles * ROM_FILE_ENTRY_SIZE

    def to_json(self) -> dict:
        names = (
            "dllfirst",
            "dlllast",
            "physfirst",
            "physlast",
            "nummods",
            "ram_start",
            "ram_free",
            "ram_end",
            "copy_entries",
            "copy_offset",
            "profile_len",
            "profile_offset",
            "numfiles",
            "kernel_flags",
            "fs_ram_percent",
            "drivglob_start",
            "drivglob_len",
            "cpu_type_misc",
            "extensions",
            "tracking_start",
            "tracking_len",
        )
        values: dict[str, int] = dict(zip(names, self.fields, strict=True))
        values.update(
            {
                "offset": self.offset,
                "size": ROMHDR_SIZE,
                "rom_size": self.rom_size,
                "cpu_type": self.cpu_type,
                "misc_flags": self.misc_flags,
                "module_table_offset": self.module_table_offset,
                "file_table_offset": self.file_table_offset,
                "table_end_offset": self.table_end_offset,
            }
        )
        return values


@dataclass(frozen=True)
class ROMModuleEntry:
    index: int
    offset: int
    attributes: int
    timestamp_low: int
    timestamp_high: int
    size: int
    name_pointer: int
    e32_pointer: int
    o32_pointer: int
    load_pointer: int

    def to_json(self) -> dict:
        return {
            "index": self.index,
            "offset": self.offset,
            "attributes": self.attributes,
            "timestamp_low": self.timestamp_low,
            "timestamp_high": self.timestamp_high,
            "size": self.size,
            "name_pointer": self.name_pointer,
            "e32_pointer": self.e32_pointer,
            "o32_pointer": self.o32_pointer,
            "load_pointer": self.load_pointer,
        }


@dataclass(frozen=True)
class ROMFileEntry:
    index: int
    offset: int
    attributes: int
    timestamp_low: int
    timestamp_high: int
    size: int
    compressed_size: int
    name_pointer: int
    load_pointer: int

    def to_json(self) -> dict:
        return {
            "index": self.index,
            "offset": self.offset,
            "attributes": self.attributes,
            "timestamp_low": self.timestamp_low,
            "timestamp_high": self.timestamp_high,
            "size": self.size,
            "compressed_size": self.compressed_size,
            "name_pointer": self.name_pointer,
            "load_pointer": self.load_pointer,
        }


@dataclass(frozen=True)
class O32RomSection:
    index: int
    virtual_size: int
    rva: int
    physical_size: int
    data_pointer: int
    real_address: int
    flags: int


@dataclass(frozen=True)
class E32Rom:
    offset: int
    object_count: int
    image_flags: int
    entry_rva: int
    image_base: int
    subsystem_major: int
    subsystem_minor: int
    stack_max: int
    virtual_size: int
    sect14_rva: int
    sect14_size: int
    timestamp: int
    units: tuple[tuple[int, int], ...]
    subsystem: int
    sections: tuple[O32RomSection, ...]

    @property
    def export_rva(self) -> int:
        return self.units[0][0]

    @property
    def export_size(self) -> int:
        return self.units[0][1]


@dataclass(frozen=True)
class ROMExportDirectory:
    raw_offset: int
    rva: int
    size: int
    timestamp: int
    name: str
    function_rvas: tuple[int, ...]
    export_names: tuple[str, ...]


@dataclass(frozen=True)
class PEImageSection:
    name: bytes
    rva: int
    virtual_size: int
    characteristics: int
    data: bytes


@dataclass(frozen=True)
class ResolvedModuleName:
    name: str
    offset: int
    delta: int


@dataclass(frozen=True)
class DirectModuleBuild:
    module: ROMModuleEntry
    e32: E32Rom
    e32_offset: int
    o32_offset: int
    name: ResolvedModuleName
    export: ROMExportDirectory | None


@dataclass(frozen=True)
class ECECPageLayout:
    raw_pages_per_group: int
    metadata_page_mod: int

    @property
    def logical_pages_per_group(self) -> int:
        return self.raw_pages_per_group - 1


def decode_c_string(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def decode_ascii_c_string(raw: bytes, offset: int, max_len: int = 128) -> str | None:
    if offset < 0 or offset >= len(raw):
        return None
    end = raw.find(b"\x00", offset, min(len(raw), offset + max_len))
    if end <= offset:
        return None
    value = raw[offset:end]
    if not all(0x20 <= byte < 0x7F for byte in value):
        return None
    return value.decode("ascii", errors="strict")


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def sanitize_file_name(name: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9._-]+", "_", name.strip("\\/"))
    return sanitized or "unnamed"


def unique_output_path(directory: Path, name: str, used_names: set[str]) -> Path:
    path = directory / sanitize_file_name(name)
    key = path.name.lower()
    if key not in used_names:
        used_names.add(key)
        return path
    stem = path.stem
    suffix = path.suffix
    for index in range(1, 10000):
        candidate = directory / f"{stem}_{index}{suffix}"
        key = candidate.name.lower()
        if key not in used_names:
            used_names.add(key)
            return candidate
    raise click.ClickException(f"too many duplicate output names for {name}")


_bch = bchlib.BCH(4, 0x201B)

# BCH parameters: GF(2^13), t=4, primitive polynomial 0x201B (x^13+x^4+x^3+x+1).
# ECC input: data[0:512] + oob[0:9] = 521 bytes. ECC parity stored at oob[9:16] (7 bytes).
_ERASED_CHUNK = b"\xFF" * RAW_CHUNK_SIZE


@dataclass
class ECCStats:
    ok: int = 0
    corrected: int = 0
    uncorrectable: int = 0
    raw_write: int = 0       # oob_all_ff: data present, ECC never written
    corrected_chunks: list[dict] = field(default_factory=list)
    uncorrectable_chunks: list[dict] = field(default_factory=list)

    def merge(self, other: "ECCStats") -> None:
        self.ok += other.ok
        self.corrected += other.corrected
        self.uncorrectable += other.uncorrectable
        self.raw_write += other.raw_write
        self.corrected_chunks.extend(other.corrected_chunks)
        self.uncorrectable_chunks.extend(other.uncorrectable_chunks)


def _ecc_correct_chunk(data: bytes, oob: bytes) -> tuple[bytes, str, int]:
    """Return (corrected_data_512, status, n_bits).

    status: 'ok', 'corrected', 'raw_write', 'mirror_mismatch', 'ecc_uncorrectable'.
    n_bits is the flip count for 'corrected', 0 otherwise.
    Uncorrectable chunks are returned as-is (original bytes preserved).
    """
    stored_ecc = oob[9:16]
    inp = bytearray(data + oob[:9])
    computed = _bch.encode(bytes(inp))

    if computed == stored_ecc:
        return data, "ok", 0

    if oob == b"\xFF" * OOB_SIZE:
        # OOB was never written; data is raw-written without ECC.  Treat as valid.
        return data, "raw_write", 0

    n = _bch.decode(inp, stored_ecc)
    if n > 0:
        # errloc holds bit positions; correct() flips them in inp.
        _bch.correct(inp)
        return bytes(inp[:CHUNK_SIZE]), "corrected", n
    if n == 0:
        # Syndrome is zero: only ECC padding bits differ, data is fine.
        return data, "ok", 0
    # n < 0: beyond correction capability.
    # Distinguish OOB mirror corruption (oob[4:9] should equal data[4:9]).
    if oob[4:9] != data[4:9]:
        return data, "mirror_mismatch", 0
    return data, "ecc_uncorrectable", 0


def normalize_plain(page: bytes) -> bytes:
    return page[:PAGE_SIZE]


def normalize_interleaved(page: bytes) -> bytes:
    return b"".join(page[i * RAW_CHUNK_SIZE : i * RAW_CHUNK_SIZE + CHUNK_SIZE] for i in range(CHUNKS_PER_PAGE))


def normalize_interleaved_ecc(page: bytes, page_index: int, stats: ECCStats) -> bytes:
    chunks = []
    for i in range(CHUNKS_PER_PAGE):
        off = i * RAW_CHUNK_SIZE
        raw = page[off : off + RAW_CHUNK_SIZE]
        if raw == _ERASED_CHUNK:
            chunks.append(raw[:CHUNK_SIZE])
            continue
        data, status, n_bits = _ecc_correct_chunk(raw[:CHUNK_SIZE], raw[CHUNK_SIZE:])
        chunks.append(data)
        if status == "ok":
            stats.ok += 1
        elif status == "corrected":
            stats.corrected += 1
            stats.corrected_chunks.append({"page": page_index, "chunk": i, "n_bits": n_bits})
        elif status == "raw_write":
            stats.raw_write += 1
        else:
            stats.uncorrectable += 1
            stats.uncorrectable_chunks.append({"page": page_index, "chunk": i, "type": status})
    return b"".join(chunks)


def normalize_nbt_page(page: bytes, page_index: int, stats: ECCStats) -> bytes:
    if page[PAGE_SIZE:] == b"\xFF" * (RAW_PAGE_SIZE - PAGE_SIZE):
        return normalize_plain(page)
    return normalize_interleaved_ecc(page, page_index, stats)


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


def normalize_raw_nand(raw_path: Path, clean_path: Path, candidate: PTBCandidate) -> ECCStats:
    nbt_entry = find_entry(candidate.entries, "NBT")
    nbt_start_page = nbt_entry.start_block * candidate.geometry.pages_per_block
    nbt_end_page = (nbt_entry.start_block + nbt_entry.block_count) * candidate.geometry.pages_per_block

    stats = ECCStats()
    with raw_path.open("rb") as fi, clean_path.open("wb") as fo:
        for page_index in range(candidate.geometry.total_pages):
            page = fi.read(RAW_PAGE_SIZE)
            if len(page) != RAW_PAGE_SIZE:
                raise click.ClickException(f"short read at raw page {page_index}")
            if nbt_start_page <= page_index < nbt_end_page:
                fo.write(normalize_nbt_page(page, page_index, stats))
            else:
                fo.write(normalize_interleaved_ecc(page, page_index, stats))
    return stats


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


def ecec_logical_to_raw_offset(logical_offset: int, logical_pages_per_raw_group: int, metadata_page_index: int) -> int:
    if logical_offset < 0:
        raise ValueError("negative ECEC logical offset")
    if logical_pages_per_raw_group <= 0:
        raise ValueError("invalid ECEC raw group size")
    if not (0 <= metadata_page_index <= logical_pages_per_raw_group):
        raise ValueError("invalid ECEC metadata page index")

    logical_page, page_offset = divmod(logical_offset, PAGE_SIZE)
    group_index, logical_page_in_group = divmod(logical_page, logical_pages_per_raw_group)
    raw_page_in_group = logical_page_in_group
    if logical_page_in_group >= metadata_page_index:
        raw_page_in_group += 1
    raw_pages_per_group = logical_pages_per_raw_group + 1
    return (group_index * raw_pages_per_group + raw_page_in_group) * PAGE_SIZE + page_offset


def ecec_raw_to_logical_size(raw_size: int, logical_pages_per_raw_group: int, metadata_page_index: int) -> int:
    if raw_size < 0:
        raise ValueError("negative ECEC raw size")
    if logical_pages_per_raw_group <= 0:
        raise ValueError("invalid ECEC raw group size")
    if not (0 <= metadata_page_index <= logical_pages_per_raw_group):
        raise ValueError("invalid ECEC metadata page index")

    raw_pages_per_group = logical_pages_per_raw_group + 1
    full_pages, trailing_bytes = divmod(raw_size, PAGE_SIZE)
    full_groups, raw_pages_in_group = divmod(full_pages, raw_pages_per_group)
    logical_pages = full_groups * logical_pages_per_raw_group
    logical_pages += sum(1 for page_index in range(raw_pages_in_group) if page_index != metadata_page_index)
    logical_size = logical_pages * PAGE_SIZE
    if trailing_bytes and raw_pages_in_group != metadata_page_index:
        logical_size += trailing_bytes
    return logical_size


def ecec_pointer_to_offset(image: ECECImage, pointer: int) -> int | None:
    logical_offset = pointer - image.load_base
    if logical_offset < 0 or logical_offset >= image.logical_size:
        return None
    raw_offset = ecec_logical_to_raw_offset(logical_offset, image.logical_pages_per_raw_group, image.metadata_page_index)
    if raw_offset >= image.span_size:
        return None
    return raw_offset


def is_ecec_metadata_page(page: bytes) -> bool:
    if len(page) != PAGE_SIZE:
        return False
    marker, erased0, erased1 = struct.unpack_from("<III", page, 4)
    return marker == 0xFFFBFFFD and erased0 == 0xFFFFFFFF and erased1 == 0xFFFFFFFF and page.count(0xFF) > PAGE_SIZE // 2


def infer_ecec_page_layout(raw: bytes) -> ECECPageLayout:
    metadata_pages = [
        page_index
        for page_index in range(len(raw) // PAGE_SIZE)
        if is_ecec_metadata_page(raw[page_index * PAGE_SIZE : (page_index + 1) * PAGE_SIZE])
    ]
    if len(metadata_pages) < 2:
        raise click.ClickException("unable to locate ECEC metadata pages")

    period = 0
    for current, next_page in zip(metadata_pages, metadata_pages[1:]):
        period = math.gcd(period, next_page - current)
    if not (2 <= period <= MAX_ECEC_LOGICAL_PAGES_PER_RAW_GROUP + 1):
        raise click.ClickException("invalid ECEC metadata page period")

    metadata_mods = {page_index % period for page_index in metadata_pages}
    if len(metadata_mods) != 1:
        raise click.ClickException("inconsistent ECEC metadata page positions")

    return ECECPageLayout(raw_pages_per_group=period, metadata_page_mod=metadata_mods.pop())


def module_name_at(raw: bytes, offset: int) -> str | None:
    name = decode_ascii_c_string(raw, offset)
    if name is None or MODULE_NAME_RE.fullmatch(name) is None:
        return None
    return name


def resolve_module_name(raw: bytes, image: ECECImage, module: ROMModuleEntry) -> ResolvedModuleName | None:
    offset = ecec_pointer_to_offset(image, module.name_pointer)
    if offset is None:
        return None
    name = module_name_at(raw, offset)
    if name is None:
        return None
    return ResolvedModuleName(name=name, offset=offset, delta=offset - (module.name_pointer - image.load_base))


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
    chain_off = first_blob.find(CHAIN_INFO_NAME)
    if chain_off < 0:
        return {}

    bases = {header["load_base"] for header in headers}
    spans: dict[int, int] = {}
    start = max(0, chain_off - 0x200)
    end = min(len(first_blob), chain_off + 0x800)
    for rec_off in range(start & ~3, end - 0x10 + 1, 4):
        load_base, logical_size, order, reserved = struct.unpack_from("<4I", first_blob, rec_off)
        if load_base not in bases:
            continue
        if logical_size <= 0 or logical_size % PAGE_SIZE:
            continue
        if reserved != 0:
            continue
        if (order >> 16) != 1:
            continue
        spans[load_base] = logical_size
    return spans


def find_ecec_images(raw: bytes) -> list[ECECImage]:
    headers = scan_ecec_headers(raw)
    logical_spans = scan_chain_spans(raw, headers)
    page_layout = infer_ecec_page_layout(raw)
    logical_pages_per_raw_group = page_layout.logical_pages_per_group
    images: list[ECECImage] = []
    for index, header in enumerate(headers):
        next_offset = headers[index + 1]["offset"] if index + 1 < len(headers) else len(raw)
        fallback_span_size = next_offset - header["offset"]
        metadata_page_index = (page_layout.metadata_page_mod - header["offset"] // PAGE_SIZE) % page_layout.raw_pages_per_group
        logical_size = logical_spans.get(
            header["load_base"],
            ecec_raw_to_logical_size(fallback_span_size, logical_pages_per_raw_group, metadata_page_index),
        )
        span_size = ecec_logical_to_raw_offset(logical_size, logical_pages_per_raw_group, metadata_page_index)
        images.append(
            ECECImage(
                index=index,
                offset=header["offset"],
                span_size=min(span_size, len(raw) - header["offset"]),
                logical_size=logical_size,
                logical_pages_per_raw_group=logical_pages_per_raw_group,
                metadata_page_index=metadata_page_index,
                load_base=header["load_base"],
                header_field_44=header["field_44"],
                header_field_48=header["field_48"],
            )
        )
    return images


def is_romhdr_candidate(raw: bytes, offset: int, image: ECECImage) -> bool:
    fields = struct.unpack_from("<21I", raw, offset)
    physfirst = fields[2]
    physlast = fields[3]
    nummods = fields[4]
    numfiles = fields[12]
    cpu_type = fields[17] & 0xFFFF

    if physfirst != image.load_base or physlast <= physfirst:
        return False
    if physlast > image.load_base + image.logical_size:
        return False
    if not (1 <= nummods <= MAX_ROM_MODULES):
        return False
    if numfiles > MAX_ROM_FILES:
        return False
    if cpu_type != ARM_CPU_TYPE:
        return False

    table_end = offset + ROMHDR_SIZE + nummods * ROM_MODULE_ENTRY_SIZE + numfiles * ROM_FILE_ENTRY_SIZE
    return table_end <= len(raw)


def find_romhdr(raw: bytes, image: ECECImage) -> ROMHDR | None:
    offset = ecec_pointer_to_offset(image, image.header_field_44)
    if offset is None or offset + ROMHDR_SIZE > len(raw):
        return None
    if not is_romhdr_candidate(raw, offset, image):
        return None

    return ROMHDR(offset=offset, fields=struct.unpack_from("<21I", raw, offset))


def parse_rom_modules(raw: bytes, romhdr: ROMHDR) -> list[ROMModuleEntry]:
    modules: list[ROMModuleEntry] = []
    for index in range(romhdr.nummods):
        offset = romhdr.module_table_offset + index * ROM_MODULE_ENTRY_SIZE
        fields = struct.unpack_from("<8I", raw, offset)
        modules.append(
            ROMModuleEntry(
                index=index,
                offset=offset,
                attributes=fields[0],
                timestamp_low=fields[1],
                timestamp_high=fields[2],
                size=fields[3],
                name_pointer=fields[4],
                e32_pointer=fields[5],
                o32_pointer=fields[6],
                load_pointer=fields[7],
            )
        )
    return modules


def parse_rom_files(raw: bytes, romhdr: ROMHDR) -> list[ROMFileEntry]:
    files: list[ROMFileEntry] = []
    for index in range(romhdr.numfiles):
        offset = romhdr.file_table_offset + index * ROM_FILE_ENTRY_SIZE
        fields = struct.unpack_from("<7I", raw, offset)
        files.append(
            ROMFileEntry(
                index=index,
                offset=offset,
                attributes=fields[0],
                timestamp_low=fields[1],
                timestamp_high=fields[2],
                size=fields[3],
                compressed_size=fields[4],
                name_pointer=fields[5],
                load_pointer=fields[6],
            )
        )
    return files


def parse_e32_rom(raw: bytes, offset: int, o32_offset: int | None = None) -> E32Rom | None:
    if offset < 0 or offset + E32_ROM_SIZE > len(raw):
        return None

    object_count, image_flags = struct.unpack_from("<HH", raw, offset)
    if not (1 <= object_count <= MAX_E32_OBJECTS):
        return None

    entry_rva = struct.unpack_from("<I", raw, offset + 0x04)[0]
    image_base = struct.unpack_from("<I", raw, offset + 0x08)[0]
    subsystem_major, subsystem_minor = struct.unpack_from("<HH", raw, offset + 0x0C)
    stack_max = struct.unpack_from("<I", raw, offset + 0x10)[0]
    virtual_size = struct.unpack_from("<I", raw, offset + 0x14)[0]
    sect14_rva = struct.unpack_from("<I", raw, offset + 0x18)[0]
    sect14_size = struct.unpack_from("<I", raw, offset + 0x1C)[0]
    timestamp = struct.unpack_from("<I", raw, offset + 0x20)[0]
    units = tuple(struct.unpack_from("<II", raw, offset + 0x24 + index * 8) for index in range(9))
    subsystem = struct.unpack_from("<H", raw, offset + 0x6C)[0]

    if subsystem_major not in {3, 4, 5, 6}:
        return None
    if stack_max not in {0, 0x10000, 0x20000, 0x40000, 0x100000}:
        return None
    if not (0x1000 <= virtual_size <= 0x02000000):
        return None
    if entry_rva >= virtual_size + 0x100000:
        return None
    if units[0][0] and units[0][0] >= virtual_size:
        return None

    section_offset = o32_offset if o32_offset is not None else offset + E32_ROM_SIZE
    if section_offset + object_count * O32_ROM_SIZE > len(raw):
        return None

    sections: list[O32RomSection] = []
    for index in range(object_count):
        (
            virtual_size_section,
            rva,
            physical_size,
            data_pointer,
            real_address,
            flags,
        ) = struct.unpack_from("<6I", raw, section_offset + index * O32_ROM_SIZE)

        if virtual_size_section > 0x02000000 or physical_size > 0x02000000:
            return None
        if rva % PE_SECTION_ALIGNMENT:
            return None
        if flags == 0:
            return None

        sections.append(
            O32RomSection(
                index=index,
                virtual_size=virtual_size_section,
                rva=rva,
                physical_size=physical_size,
                data_pointer=data_pointer,
                real_address=real_address,
                flags=flags,
            )
        )

    return E32Rom(
        offset=offset,
        object_count=object_count,
        image_flags=image_flags,
        entry_rva=entry_rva,
        image_base=image_base,
        subsystem_major=subsystem_major,
        subsystem_minor=subsystem_minor,
        stack_max=stack_max,
        virtual_size=virtual_size,
        sect14_rva=sect14_rva,
        sect14_size=sect14_size,
        timestamp=timestamp,
        units=units,
        subsystem=subsystem,
        sections=tuple(sections),
    )


def export_local_offset(export_raw_offset: int, export_rva: int, rva: int, raw_size: int) -> int | None:
    delta = rva - export_rva
    if 0 <= delta <= EXPORT_SCAN_WINDOW and export_raw_offset + delta < raw_size:
        return export_raw_offset + delta
    return None


def parse_export_at(raw: bytes, offset: int) -> ROMExportDirectory | None:
    if offset + 0x28 > len(raw):
        return None

    (
        characteristics,
        timestamp,
        major_version,
        minor_version,
        name_rva,
        ordinal_base,
        function_count,
        name_count,
        function_table_rva,
        name_table_rva,
        ordinal_table_rva,
    ) = struct.unpack_from("<IIHHIIIIIII", raw, offset)

    if characteristics != 0:
        return None
    if major_version != 0 or minor_version != 0:
        return None
    if not (0x30000000 <= timestamp <= 0x70000000):
        return None
    if not (1 <= ordinal_base <= 0x10000):
        return None
    if not (1 <= name_count <= function_count <= 4096):
        return None
    if name_count > MAX_EXPORT_NAMES:
        return None

    window = raw[offset + 0x28 : min(len(raw), offset + EXPORT_SCAN_WINDOW)]
    candidates: list[ROMExportDirectory] = []
    for match in MODULE_NAME_BYTES_RE.finditer(window):
        name_raw_offset = offset + 0x28 + match.start()
        name = match.group()[:-1].decode("ascii", errors="strict")
        if MODULE_NAME_RE.fullmatch(name) is None:
            continue

        export_rva = name_rva - (name_raw_offset - offset)
        if export_rva <= 0:
            continue

        function_table_offset = export_local_offset(offset, export_rva, function_table_rva, len(raw))
        name_table_offset = export_local_offset(offset, export_rva, name_table_rva, len(raw))
        ordinal_table_offset = export_local_offset(offset, export_rva, ordinal_table_rva, len(raw))
        if function_table_offset is None or name_table_offset is None or ordinal_table_offset is None:
            continue
        if function_table_offset + function_count * 4 > len(raw):
            continue
        if name_table_offset + name_count * 4 > len(raw):
            continue
        if ordinal_table_offset + name_count * 2 > len(raw):
            continue

        function_rvas = tuple(struct.unpack_from("<I", raw, function_table_offset + index * 4)[0] for index in range(function_count))
        export_names: list[str] = []
        export_end = max(
            function_table_rva + function_count * 4,
            name_table_rva + name_count * 4,
            ordinal_table_rva + name_count * 2,
            name_rva + len(name) + 1,
        )
        valid = True
        for index in range(name_count):
            export_name_rva = struct.unpack_from("<I", raw, name_table_offset + index * 4)[0]
            export_name_offset = export_local_offset(offset, export_rva, export_name_rva, len(raw))
            export_name = decode_ascii_c_string(raw, export_name_offset) if export_name_offset is not None else None
            if export_name is None:
                valid = False
                break
            export_names.append(export_name)
            export_end = max(export_end, export_name_rva + len(export_name) + 1)
        if not valid:
            continue

        export_size = export_end - export_rva
        if export_size <= 0 or export_size > EXPORT_SCAN_WINDOW:
            continue

        candidates.append(
            ROMExportDirectory(
                raw_offset=offset,
                rva=export_rva,
                size=export_size,
                timestamp=timestamp,
                name=name,
                function_rvas=function_rvas,
                export_names=tuple(export_names),
            )
        )

    if len(candidates) != 1:
        return None
    return candidates[0]


def section_name(index: int, section: O32RomSection) -> bytes:
    if section.flags & 0x20:
        return b".text"
    if section.flags & 0x80:
        return b".bss"
    if section.flags & 0x40:
        if section.flags & 0x80000000:
            return b".data"
        return b".rdata"
    return f".sec{index}".encode("ascii")


def should_emit_pe_section(section: O32RomSection, sections: tuple[O32RomSection, ...]) -> bool:
    if section.physical_size != 0:
        return True
    return not any(
        other.index != section.index
        and other.physical_size > 0
        and other.rva == section.rva
        for other in sections
    )


def module_section_file_offset(
    image: ECECImage,
    section: O32RomSection,
) -> int | None:
    return ecec_pointer_to_offset(image, section.data_pointer)


def read_module_section_payload(
    raw: bytes,
    image: ECECImage,
    section: O32RomSection,
) -> tuple[bytes, bool]:
    size = section.physical_size
    if size <= 0:
        return b"", False
    source_offset = module_section_file_offset(image, section)
    if source_offset is None or source_offset < 0 or source_offset >= len(raw):
        return b"", False
    data = raw[source_offset : min(len(raw), source_offset + size)]
    if section.flags & CECOMPRESS_FLAG:
        try:
            decompressed = maybe_decompress_cecompress(data, section.virtual_size)
        except CECompressError as exc:
            raise click.ClickException(
                f"failed to decompress section {section.index} at 0x{source_offset:X}: {exc}"
            ) from exc
        if decompressed is not None:
            return decompressed, True
    return data, False


def read_module_section_data(
    raw: bytes,
    image: ECECImage,
    section: O32RomSection,
) -> bytes:
    data, _decompressed = read_module_section_payload(raw, image, section)
    return data


def pe_section_data_at(sections: list[PEImageSection], rva: int, size: int) -> bytes | None:
    if size < 0:
        return None
    for section in sections:
        span = max(section.virtual_size, len(section.data))
        if section.rva <= rva and rva + size <= section.rva + span:
            offset = rva - section.rva
            if offset + size <= len(section.data):
                return section.data[offset : offset + size]
            return None
    return None


def pe_c_string_at(sections: list[PEImageSection], rva: int, max_len: int = 128) -> str | None:
    for section in sections:
        span = max(section.virtual_size, len(section.data))
        if not (section.rva <= rva < section.rva + span):
            continue
        offset = rva - section.rva
        if offset >= len(section.data):
            return None
        end_limit = min(len(section.data), offset + max_len)
        end = section.data.find(b"\x00", offset, end_limit)
        if end <= offset:
            return None
        value = section.data[offset:end]
        if not all(0x20 <= byte < 0x7F for byte in value):
            return None
        return value.decode("ascii", errors="strict")
    return None


def is_valid_import_directory(sections: list[PEImageSection], import_rva: int, import_size: int, virtual_size: int) -> bool:
    if import_rva <= 0 or import_size < 20 or import_rva >= virtual_size:
        return False

    descriptor_count = min(import_size // 20, 64)
    valid_descriptors = 0
    for index in range(descriptor_count):
        descriptor = pe_section_data_at(sections, import_rva + index * 20, 20)
        if descriptor is None:
            return False
        original_first_thunk, _timestamp, _forwarder_chain, name_rva, first_thunk = struct.unpack("<IIIII", descriptor)
        if original_first_thunk == 0 and name_rva == 0 and first_thunk == 0:
            return valid_descriptors > 0

        name = pe_c_string_at(sections, name_rva)
        if name is None or not name.lower().endswith(".dll"):
            return False

        thunk_rva = original_first_thunk or first_thunk
        if thunk_rva <= 0 or thunk_rva >= virtual_size or first_thunk <= 0 or first_thunk >= virtual_size:
            return False
        if pe_section_data_at(sections, thunk_rva, 4) is None:
            return False
        if pe_section_data_at(sections, first_thunk, 4) is None:
            return False

        valid_descriptors += 1

    return False


def shift_export_rva(blob: bytearray, offset: int, old_rva: int, new_rva: int, size: int) -> None:
    if offset < 0 or offset + 4 > len(blob):
        return
    value = struct.unpack_from("<I", blob, offset)[0]
    if old_rva <= value < old_rva + size:
        struct.pack_into("<I", blob, offset, new_rva + value - old_rva)


def relocated_export_blob(raw: bytes, e32: E32Rom, export: ROMExportDirectory, new_rva: int) -> bytes:
    size = max(e32.export_size, export.size)
    blob = bytearray(size)
    copy_size = min(size, len(raw) - export.raw_offset)
    if copy_size > 0:
        blob[:copy_size] = raw[export.raw_offset : export.raw_offset + copy_size]

    if len(blob) < 0x28:
        return bytes(blob)

    (
        _characteristics,
        _timestamp,
        _major_version,
        _minor_version,
        name_rva,
        _ordinal_base,
        _function_count,
        name_count,
        _function_table_rva,
        name_table_rva,
        _ordinal_table_rva,
    ) = struct.unpack_from("<IIHHIIIIIII", blob, 0)

    shift_export_rva(blob, 0x0C, export.rva, new_rva, size)
    shift_export_rva(blob, 0x1C, export.rva, new_rva, size)
    shift_export_rva(blob, 0x20, export.rva, new_rva, size)
    shift_export_rva(blob, 0x24, export.rva, new_rva, size)

    if export.rva <= name_table_rva < export.rva + size:
        name_table_offset = name_table_rva - export.rva
        for index in range(name_count):
            shift_export_rva(blob, name_table_offset + index * 4, export.rva, new_rva, size)

    if name_rva and not (export.rva <= name_rva < export.rva + size):
        struct.pack_into("<I", blob, 0x0C, name_rva)

    return bytes(blob)


def find_module_export(raw: bytes, module_name: str, e32: E32Rom) -> ROMExportDirectory | None:
    needle = module_name.encode("ascii", errors="ignore") + b"\x00"
    if not needle:
        return None

    matches: list[ROMExportDirectory] = []
    name_offset = -1
    while True:
        name_offset = raw.find(needle, name_offset + 1)
        if name_offset < 0:
            break
        start = max(0, (name_offset - EXPORT_SCAN_WINDOW) & ~3)
        end = name_offset & ~3
        for offset in range(start, end + 1, 4):
            export = parse_export_at(raw, offset)
            if export is None:
                continue
            if export.name.lower() != module_name.lower():
                continue
            if export.rva != e32.export_rva:
                continue
            if e32.export_size and export.size > max(e32.export_size, EXPORT_SCAN_WINDOW):
                continue
            if not all(0 < rva < e32.virtual_size for rva in export.function_rvas):
                continue
            matches.append(export)

    unique: dict[int, ROMExportDirectory] = {export.raw_offset: export for export in matches}
    if not unique:
        return None
    return sorted(unique.values(), key=lambda export: (export.raw_offset, export.size))[0]


def relocate_in_image_pointers(data: bytes, old_base: int, new_base: int, virtual_size: int) -> bytes:
    if old_base == new_base or len(data) < 4:
        return data

    blob = bytearray(data)
    old_end = old_base + virtual_size
    for offset in range(0, len(blob) - 3, 4):
        value = struct.unpack_from("<I", blob, offset)[0]
        if old_base <= value < old_end:
            struct.pack_into("<I", blob, offset, new_base + value - old_base)
    return bytes(blob)


def build_direct_pe_sections(raw: bytes, image: ECECImage, module: ROMModuleEntry, e32: E32Rom, module_name: str) -> tuple[list[PEImageSection], ROMExportDirectory | None, int, int, int, int]:
    export = find_module_export(raw, module_name, e32) if e32.export_rva else None
    sections: list[PEImageSection] = []
    for index, section in enumerate(e32.sections):
        if not should_emit_pe_section(section, e32.sections):
            continue
        data, decompressed = read_module_section_payload(raw, image, section)
        data = relocate_in_image_pointers(data, e32.image_base, module.load_pointer, e32.virtual_size)
        characteristics = section.flags & ~CECOMPRESS_FLAG if decompressed else section.flags
        sections.append(
            PEImageSection(
                name=section_name(index, section),
                rva=section.rva,
                virtual_size=max(section.virtual_size, len(data)),
                characteristics=characteristics,
                data=data,
            )
        )

    export_rva = 0
    export_size = 0
    if export is not None and export.size > 0:
        export_rva = align_up(
            max(e32.virtual_size, *(section.rva + section_span_like(section.virtual_size, len(section.data)) for section in sections)),
            PE_SECTION_ALIGNMENT,
        )
        export_data = relocated_export_blob(raw, e32, export, export_rva)
        export_size = len(export_data)
        if export_data:
            sections.append(
                PEImageSection(
                    name=b".edata",
                    rva=export_rva,
                    virtual_size=len(export_data),
                    characteristics=0x40000040,
                    data=export_data,
                )
            )

    import_rva, import_size = e32.units[1]
    if not is_valid_import_directory(sections, import_rva, import_size, e32.virtual_size):
        import_rva = 0
        import_size = 0

    return sorted(sections, key=lambda section: section.rva), export, export_rva, export_size, import_rva, import_size


def section_span_like(virtual_size: int, data_size: int) -> int:
    return align_up(max(virtual_size, data_size), PE_SECTION_ALIGNMENT)


def build_pe_image_from_sections(
    e32: E32Rom,
    image_base: int,
    image_sections: list[PEImageSection],
    export_rva: int,
    export_size: int,
    import_rva: int,
    import_size: int,
) -> bytes:
    pe_header_offset = PE_DOS_HEADER_SIZE
    headers_size = align_up(
        pe_header_offset + 4 + 20 + PE_OPTIONAL_HEADER_SIZE + len(image_sections) * 40,
        PE_FILE_ALIGNMENT,
    )

    raw_cursor = headers_size
    section_layouts: list[dict] = []
    for section in image_sections:
        raw_size = align_up(len(section.data), PE_FILE_ALIGNMENT) if section.data else 0
        section_layouts.append({"section": section, "raw_pointer": raw_cursor if raw_size else 0, "raw_size": raw_size})
        raw_cursor += raw_size

    pe = bytearray(raw_cursor)
    pe[:2] = b"MZ"
    struct.pack_into("<I", pe, 0x3C, pe_header_offset)
    pe[pe_header_offset : pe_header_offset + 4] = b"PE\x00\x00"

    coff_offset = pe_header_offset + 4
    struct.pack_into(
        "<HHIIIHH",
        pe,
        coff_offset,
        PE_MACHINE_ARM,
        len(image_sections),
        e32.timestamp,
        0,
        0,
        PE_OPTIONAL_HEADER_SIZE,
        e32.image_flags,
    )

    code_size = sum(layout["raw_size"] for layout in section_layouts if layout["section"].characteristics & 0x20)
    initialized_size = sum(layout["raw_size"] for layout in section_layouts if layout["section"].characteristics & 0x40)
    optional_offset = coff_offset + 20
    base_of_code = next((section.rva for section in image_sections if section.characteristics & 0x20), 0)
    base_of_data = next((section.rva for section in image_sections if section.characteristics & 0x40 and not section.characteristics & 0x20), 0)
    size_of_image = align_up(
        max(section.rva + section_span_like(section.virtual_size, len(section.data)) for section in image_sections),
        PE_SECTION_ALIGNMENT,
    )

    struct.pack_into(
        "<HBBIIIIII",
        pe,
        optional_offset,
        0x10B,
        0,
        0,
        code_size,
        initialized_size,
        0,
        e32.entry_rva,
        base_of_code,
        base_of_data,
    )
    struct.pack_into(
        "<III",
        pe,
        optional_offset + 0x1C,
        image_base,
        PE_SECTION_ALIGNMENT,
        PE_FILE_ALIGNMENT,
    )
    struct.pack_into("<HHHHHH", pe, optional_offset + 0x28, 5, 0, 0, 0, e32.subsystem_major, e32.subsystem_minor)
    struct.pack_into("<I", pe, optional_offset + 0x34, 0)
    struct.pack_into("<I", pe, optional_offset + 0x38, size_of_image)
    struct.pack_into("<I", pe, optional_offset + 0x3C, headers_size)
    struct.pack_into("<I", pe, optional_offset + 0x40, 0)
    struct.pack_into("<HH", pe, optional_offset + 0x44, e32.subsystem, 0)
    struct.pack_into("<IIIIII", pe, optional_offset + 0x48, e32.stack_max, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into("<II", pe, optional_offset + 0x60, export_rva, export_size)
    struct.pack_into("<II", pe, optional_offset + 0x68, import_rva, import_size)

    section_header_offset = optional_offset + PE_OPTIONAL_HEADER_SIZE
    for index, layout in enumerate(section_layouts):
        section = layout["section"]
        header_offset = section_header_offset + index * 40
        pe[header_offset : header_offset + 8] = section.name[:8].ljust(8, b"\x00")
        struct.pack_into(
            "<IIIIIIHHI",
            pe,
            header_offset + 8,
            section.virtual_size,
            section.rva,
            layout["raw_size"],
            layout["raw_pointer"],
            0,
            0,
            0,
            0,
            section.characteristics,
        )

        if layout["raw_size"] and section.data:
            copy_size = min(len(section.data), layout["raw_size"])
            pe[layout["raw_pointer"] : layout["raw_pointer"] + copy_size] = section.data[:copy_size]

    return bytes(pe)


def build_direct_pe_image(
    raw: bytes,
    image: ECECImage,
    module: ROMModuleEntry,
    e32: E32Rom,
    module_name: str,
) -> tuple[bytes, ROMExportDirectory | None, int, int, int, int]:
    sections, export, export_rva, export_size, import_rva, import_size = build_direct_pe_sections(raw, image, module, e32, module_name)
    return (
        build_pe_image_from_sections(e32, module.load_pointer, sections, export_rva, export_size, import_rva, import_size),
        export,
        export_rva,
        export_size,
        import_rva,
        import_size,
    )


def resolve_direct_modules(raw: bytes, image: ECECImage, romhdr: ROMHDR, modules: list[ROMModuleEntry]) -> list[DirectModuleBuild]:
    if romhdr.nummods != len(modules):
        return []
    resolved: list[DirectModuleBuild] = []
    for module in modules:
        name = resolve_module_name(raw, image, module)
        if name is None:
            continue
        e32_offset = ecec_pointer_to_offset(image, module.e32_pointer)
        o32_offset = ecec_pointer_to_offset(image, module.o32_pointer)
        if e32_offset is None or o32_offset is None:
            continue
        e32 = parse_e32_rom(raw, e32_offset, o32_offset)
        if e32 is None:
            continue
        resolved.append(DirectModuleBuild(module=module, e32=e32, e32_offset=e32_offset, o32_offset=o32_offset, name=name, export=None))
    return resolved


def write_rebuilt_modules(raw: bytes, image: ECECImage, out_dir: Path, image_path: Path) -> list[dict]:
    romhdr = find_romhdr(raw, image)
    if romhdr is None:
        return []

    modules = parse_rom_modules(raw, romhdr)
    direct_modules = resolve_direct_modules(raw, image, romhdr, modules)
    if not direct_modules:
        return []

    module_dir = out_dir / f"{image_path.stem}.modules"
    rebuilt: list[dict] = []
    used_names: set[str] = set()

    for direct in direct_modules:
        e32 = direct.e32
        module = direct.module
        name = direct.name.name
        module_dir.mkdir(parents=True, exist_ok=True)
        path = unique_output_path(module_dir, name, used_names)
        pe, export, pe_export_rva, pe_export_size, pe_import_rva, pe_import_size = build_direct_pe_image(raw, image, module, e32, name)
        image_raw_base = export.raw_offset - export.rva if export is not None else None
        path.write_bytes(pe)
        rebuilt.append(
            {
                "path": str(path.relative_to(out_dir)),
                "name": name,
                "size": path.stat().st_size,
                "name_offset": direct.name.offset,
                "name_gap_bytes": direct.name.delta,
                "e32_offset": direct.e32_offset,
                "o32_offset": direct.o32_offset,
                "export_rva": pe_export_rva,
                "export_size": pe_export_size,
                "rom_export_rva": export.rva if export is not None else e32.export_rva,
                "rom_export_size": export.size if export is not None else e32.export_size,
                "import_rva": pe_import_rva,
                "import_size": pe_import_size,
                "rom_import_rva": e32.units[1][0],
                "rom_import_size": e32.units[1][1],
                "export_name_count": len(export.export_names) if export is not None else 0,
                "export_names": list(export.export_names) if export is not None else [],
                "image_raw_base": image_raw_base,
                "image_base": e32.image_base,
                "ida_image_base": module.load_pointer,
                "entry_rva": e32.entry_rva,
                "virtual_size": e32.virtual_size,
                "section_count": e32.object_count,
                "sections": [
                    {
                        "index": section.index,
                        "rva": section.rva,
                        "virtual_size": section.virtual_size,
                        "physical_size": section.physical_size,
                        "data_pointer": section.data_pointer,
                        "real_address": section.real_address,
                        "flags": section.flags,
                        "source_offset": module_section_file_offset(image, section),
                    }
                    for section in e32.sections
                ],
                "toc_index": module.index,
                "toc_attributes": module.attributes,
                "toc_size": module.size,
                "toc_name_pointer": module.name_pointer,
                "toc_e32_pointer": module.e32_pointer,
                "toc_o32_pointer": module.o32_pointer,
                "toc_load_pointer": module.load_pointer,
            }
        )

    return rebuilt


def inspect_ecec_image(raw: bytes, image: ECECImage) -> dict:
    romhdr = find_romhdr(raw, image)
    if romhdr is None:
        return {}

    return {
        "romhdr": romhdr.to_json(),
        "modules": [module.to_json() for module in parse_rom_modules(raw, romhdr)],
        "files": [file_entry.to_json() for file_entry in parse_rom_files(raw, romhdr)],
    }


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
        image_blob = binfs[image.offset : image.offset + image.span_size]
        ecec = image.to_json()
        ecec.update(inspect_ecec_image(image_blob, image))
        rebuilt_modules = write_rebuilt_modules(image_blob, image, out_dir, path)
        if rebuilt_modules:
            ecec["rebuilt_modules"] = rebuilt_modules
        views.append(
            {
                "path": path.name,
                "kind": "nk_ecec_image",
                "source": binfs_path.name,
                "source_offset": image.offset,
                "size": image.span_size,
                "load_base": image.load_base,
                "ecec": ecec,
            }
        )

    return views


def write_metadata(raw_path: Path, out_dir: Path, candidate: PTBCandidate, views: list[dict], ecc_stats: ECCStats) -> None:
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
        "normalize": {
            "ok": ecc_stats.ok,
            "corrected": ecc_stats.corrected,
            "raw_write": ecc_stats.raw_write,
            "uncorrectable": ecc_stats.uncorrectable,
            "corrected_chunks": ecc_stats.corrected_chunks,
            "uncorrectable_chunks": ecc_stats.uncorrectable_chunks,
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
    ecc_stats = normalize_raw_nand(raw_path, clean_path, candidate)
    click.echo(
        f"ECC: {ecc_stats.corrected} corrected, "
        f"{ecc_stats.raw_write} raw-write (no ECC stored), "
        f"{ecc_stats.uncorrectable} uncorrectable (passed through)"
    )
    if ecc_stats.uncorrectable:
        click.echo(f"  Warning: {ecc_stats.uncorrectable} chunk(s) had unrecoverable bit errors and were passed through unchanged", err=True)

    click.echo("Splitting PTB partitions...")
    for entry in candidate.entries:
        copy_partition(clean_path, out_dir, entry, candidate.geometry)
        click.echo(f"  -> {entry.tag}.raw")

    click.echo("Writing analysis views...")
    views = write_analysis_views(out_dir)
    for view in views:
        click.echo(f"  -> {view['path']}")

    write_metadata(raw_path, out_dir, candidate, views, ecc_stats)
    click.echo("  -> nand_extract.json")


if __name__ == "__main__":
    main()
