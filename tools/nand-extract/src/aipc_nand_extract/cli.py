"""Extract PTB-indexed partitions from an AIPC NAND dump."""

import json
import struct
from dataclasses import dataclass
from pathlib import Path

import click

BLOCK_SIZE = 0x20000
PTB_ENTRY_SIZE = 0x30
PTB_MAX_ENTRIES = 32


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

    @property
    def offset(self) -> int:
        return self.start_block * BLOCK_SIZE

    @property
    def size(self) -> int:
        return self.block_count * BLOCK_SIZE

    def to_json(self) -> dict:
        return {
            "index": self.index,
            "tag": self.tag,
            "filename": self.filename,
            "unk0": self.unk0,
            "flags": self.flags,
            "start_block": self.start_block,
            "block_count": self.block_count,
            "offset": self.offset,
            "size": self.size,
            "load_addr": self.load_addr,
        }


def decode_c_string(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def find_ptb(data: bytes) -> int | None:
    needle = b"PTB\x00"
    start = max(0, len(data) - 0x4000000)
    off = len(data)
    while True:
        pos = data.rfind(needle, start, off)
        if pos == -1:
            return None
        if pos % BLOCK_SIZE == 0:
            return pos
        off = pos


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


def parse_ptb_table(ptb_data: bytes) -> tuple[int, list[PTBEntry]]:
    best_offset = -1
    best_entries: list[PTBEntry] = []
    pos = -1
    while True:
        pos = ptb_data.find(b"NBT\x00", pos + 1)
        if pos == -1:
            break
        if pos < 4:
            continue
        table_offset = pos - 4
        if table_offset % 4:
            continue

        entries: list[PTBEntry] = []
        for index in range(PTB_MAX_ENTRIES):
            off = table_offset + index * PTB_ENTRY_SIZE
            end = off + PTB_ENTRY_SIZE
            if end > len(ptb_data):
                break
            entry = parse_ptb_entry(ptb_data[off:end], index)
            entries.append(entry)
            if entry.tag == "END":
                break

        if entries and entries[0].tag == "NBT" and entries[-1].tag == "END" and len(entries) > len(best_entries):
            best_offset = table_offset
            best_entries = entries

    if best_offset < 0:
        raise ValueError("PTB entry table not found")
    return best_offset, best_entries


def find_entry(entries: list[PTBEntry], tag: str) -> PTBEntry | None:
    for entry in entries:
        if entry.tag == tag:
            return entry
    return None


def write_raw_partition(data: bytes, out_dir: Path, stem: str, entry: PTBEntry) -> Path:
    path = out_dir / f"{entry.tag}.raw"
    path.write_bytes(data[entry.offset : entry.offset + entry.size])
    return path


def extract_nboot_nb0(raw: bytes, out_dir: Path, stem: str) -> dict | None:
    if raw[4:12] != b"ANYKA382":
        return None
    chunks_per_page, page_count, _, _ = struct.unpack_from("<BBBB", raw, 0x0C)
    image_type = struct.unpack_from("<I", raw, 0x20)[0]
    page_size = chunks_per_page * 512
    payload_size = page_count * page_size
    payload = raw[page_size : page_size + payload_size]
    path = out_dir / f"{stem}.nb0"
    path.write_bytes(payload)
    script_path = out_dir / "nboot_ddr_init.txt"
    with open(script_path, "w") as f:
        f.write("# nboot register init script\n")
        f.write("# Format: address value\n")
        for i in range(32):
            off = 0x24 + i * 8
            if off + 8 > page_size:
                break
            addr = struct.unpack_from("<I", raw, off)[0]
            val = struct.unpack_from("<I", raw, off + 4)[0]
            if addr == 0x88888888:
                f.write(f"END        0x{val:08X}\n")
                break
            if addr == 0x66668888:
                f.write(f"DELAY      {val} ticks\n")
            else:
                f.write(f"0x{addr:08X} 0x{val:08X}\n")
    return {
        "path": path.name,
        "image_type": image_type,
        "page_size": page_size,
        "payload_pages": page_count,
        "payload_offset": page_size,
        "payload_size": payload_size,
        "ddr_init_path": script_path.name,
    }


def extract_img_nb0(raw: bytes, out_dir: Path, stem: str) -> dict | None:
    img_offset = raw.find(b"IMG\x00")
    if img_offset < 0:
        return None
    img_type = decode_c_string(raw[img_offset + 4 : img_offset + 8])
    filename = decode_c_string(raw[img_offset + 8 : img_offset + 24])
    load_addr = struct.unpack_from("<I", raw, img_offset + 0x18)[0]
    region_size = struct.unpack_from("<I", raw, img_offset + 0x1C)[0]
    nb0 = raw[img_offset + 0x2C : img_offset + region_size]
    end = len(nb0)
    while end > 0 and nb0[end - 1] in (0x00, 0xFF):
        end -= 1
    end = (end + 3) & ~3
    path = out_dir / f"{stem}.nb0"
    path.write_bytes(nb0[:end])
    return {
        "path": path.name,
        "img_offset": img_offset,
        "img_type": img_type,
        "filename": filename,
        "load_addr": load_addr,
        "region_size": region_size,
        "nb0_size": end,
    }


def scan_ecec_headers(raw: bytes) -> list[dict]:
    headers = []
    for offset in range(0, len(raw) - 0x4C, 0x800):
        if raw[offset + 0x40 : offset + 0x44] != b"ECEC":
            continue
        field_44 = struct.unpack_from("<I", raw, offset + 0x44)[0]
        field_48 = struct.unpack_from("<I", raw, offset + 0x48)[0]
        if field_44 <= field_48:
            continue
        headers.append(
            {
                "offset": offset,
                "header_field_44": field_44,
                "header_field_48": field_48,
                "load_base": field_44 - field_48,
            }
        )
    return headers


def find_u32(raw: bytes, value: int) -> list[int]:
    needle = struct.pack("<I", value)
    hits = []
    pos = -4
    while True:
        pos = raw.find(needle, pos + 4)
        if pos < 0:
            return hits
        if pos % 4 == 0:
            hits.append(pos)


def scan_chain_spans(raw: bytes, headers: list[dict]) -> dict[int, int]:
    if len(headers) < 2:
        return {}

    first_blob = raw[: headers[1]["offset"]]
    chain_off = first_blob.find(b"@chain information")
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
                    if load_base in bases and span_size and span_size % 0x800 == 0:
                        spans[load_base] = span_size
            if spans:
                return spans
    return spans


def find_ecec_images(raw: bytes) -> list[dict]:
    headers = scan_ecec_headers(raw)
    spans = scan_chain_spans(raw, headers)
    images = []
    for index, header in enumerate(headers):
        next_offset = headers[index + 1]["offset"] if index + 1 < len(headers) else len(raw)
        size = spans.get(header["load_base"], next_offset - header["offset"])
        images.append(
            {
                "offset": header["offset"],
                "size": min(size, len(raw) - header["offset"]),
                "load_base": header["load_base"],
                "header_field_44": header["header_field_44"],
                "header_field_48": header["header_field_48"],
            }
        )
    return images


def write_ecec_images(raw: bytes, out_dir: Path, images: list[dict]) -> list[dict]:
    written = []
    for index, image in enumerate(images):
        path = out_dir / f"nk_ecec_{index:02d}.raw"
        path.write_bytes(raw[image["offset"] : image["offset"] + image["size"]])
        written.append({**image, "path": path.name})
    return written


def entry_stem(entry: PTBEntry) -> str:
    return {
        "NBT": "nboot",
        "IPL": "eboot",
        "BAK": "eboot_bak",
        "NK": "nk",
    }.get(entry.tag, entry.tag.lower())


@click.command()
@click.argument("nand_image", type=click.Path(exists=True))
@click.option(
    "-o",
    "--output",
    type=click.Path(),
    default=None,
    help="Output directory. Default: <nand_image_dir>/extracted.",
)
def main(nand_image: str, output: str | None) -> None:
    nand_path = Path(nand_image)
    out_dir = Path(output) if output else nand_path.parent / "extracted"
    out_dir.mkdir(parents=True, exist_ok=True)

    data = nand_path.read_bytes()
    ptb_offset = find_ptb(data)
    if ptb_offset is None:
        raise click.ClickException("PTB not found")

    ptb_raw = data[ptb_offset : ptb_offset + 0x1000]
    (out_dir / "ptb.raw").write_bytes(ptb_raw)

    version = decode_c_string(ptb_raw[4:8])
    table_offset, entries = parse_ptb_table(ptb_raw)
    metadata: dict = {
        "nand_image": nand_path.name,
        "nand_size": len(data),
        "block_size": BLOCK_SIZE,
        "ptb": {
            "offset": ptb_offset,
            "version": version,
            "table_offset": table_offset,
            "raw_path": "ptb.raw",
            "entries": [entry.to_json() for entry in entries],
        },
        "outputs": {},
    }

    click.echo(f"Reading {nand_path} ...")
    click.echo(f"  Size: {len(data)} bytes ({len(data) // (1024 * 1024)} MB)")
    click.echo(f"Output: {out_dir}")
    click.echo(f"PTB: 0x{ptb_offset:08X}, version {version}, table 0x{table_offset:X}")

    for entry in entries:
        if entry.tag == "END":
            continue
        stem = entry_stem(entry)
        raw_path = write_raw_partition(data, out_dir, stem, entry)
        item = {
            "entry": entry.to_json(),
            "raw_path": raw_path.name,
            "raw_is_partition_slice": True,
        }
        raw = raw_path.read_bytes()
        nb0_info = None
        if entry.tag == "NBT":
            nb0_info = extract_nboot_nb0(raw, out_dir, stem)
        elif entry.tag in {"IPL", "BAK"}:
            nb0_info = extract_img_nb0(raw, out_dir, stem)
        if nb0_info is not None:
            item["derived_nb0"] = nb0_info
        if entry.tag == "NK":
            item["ecec_images"] = write_ecec_images(raw, out_dir, find_ecec_images(raw))
        metadata["outputs"][stem] = item

    json_path = out_dir / "ptb.json"
    json_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    click.echo(f"  -> {json_path.name}")
    for name, item in metadata["outputs"].items():
        click.echo(f"  -> {item['raw_path']}")
        if "derived_nb0" in item:
            click.echo(f"  -> {item['derived_nb0']['path']}")
            if "ddr_init_path" in item["derived_nb0"]:
                click.echo(f"  -> {item['derived_nb0']['ddr_init_path']}")
        for image in item.get("ecec_images", []):
            click.echo(f"  -> {image['path']}")


if __name__ == "__main__":
    main()
