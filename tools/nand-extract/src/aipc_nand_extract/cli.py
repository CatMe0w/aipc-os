"""
Extract boot images from an AIPC netbook WinCE NAND dump.

Partition layout (512 MB NAND, 2048-byte pages, 128 or 256 KB blocks):

  Offset       Size    Content
  0x00000000   128KB   nboot (ANYKA382 type-6 DDR image, loaded by bootrom)
  0x00020000   128KB   (reserved)
  0x00040000   512KB   eboot IPL (IMG wrapper + WinCE EBOOT binary)
  0x000C0000   256KB   eboot tail / config data
  0x00240000   512KB   eboot BAK (backup copy)
  0x00480000   ~68MB   NK / WinCE OS image region
  0x03E00000+          FAT filesystem
  0x1FF60000   4KB     PTB (Partition Table Block)
"""

import struct
from pathlib import Path

import click


def extract_nboot(data: bytes, out_dir: Path) -> None:
    """Extract nboot from ANYKA382 header at offset 0."""
    sig = data[4:12]
    if sig != b"ANYKA382":
        click.echo("  WARNING: ANYKA382 signature not found at offset 0x04")
        return

    counts = struct.unpack_from("<BBBB", data, 0x0C)
    chunks_per_page = counts[0]
    page_count = counts[1]
    image_type = struct.unpack_from("<I", data, 0x20)[0]
    page_size = chunks_per_page * 512

    type_name = {6: "DDR", 8: "L2"}.get(image_type, "?")
    click.echo(f"  Signature:       ANYKA382")
    click.echo(f"  Image type:      {image_type} ({type_name})")
    click.echo(f"  Page size:       {page_size} B ({chunks_per_page} chunks/page)")
    click.echo(f"  Payload pages:   {page_count}")

    payload_offset = page_size
    payload_size = page_count * page_size
    click.echo(f"  Payload:         0x{payload_offset:X}..0x{payload_offset + payload_size:X}"
               f" ({payload_size} bytes)")

    full = data[: page_size + payload_size]
    (out_dir / "nboot.akimg").write_bytes(full)
    click.echo(f"  -> nboot.akimg ({len(full)} B)")

    payload = data[payload_offset : payload_offset + payload_size]
    (out_dir / "nboot.nb0").write_bytes(payload)
    click.echo(f"  -> nboot.nb0 ({len(payload)} B, load addr 0x30000000)")

    script_path = out_dir / "nboot_ddr_init.txt"
    with open(script_path, "w") as f:
        f.write("# nboot register init script (type-6 DDR init)\n")
        f.write("# Format: address value  (or special tag)\n")
        script_base = 0x24
        for i in range(32):
            off = script_base + i * 8
            if off + 8 > page_size:
                break
            addr = struct.unpack_from("<I", data, off)[0]
            val = struct.unpack_from("<I", data, off + 4)[0]
            if addr == 0x88888888:
                f.write(f"END        0x{val:08X}\n")
                break
            elif addr == 0x66668888:
                f.write(f"DELAY      {val} ticks\n")
            else:
                f.write(f"0x{addr:08X} 0x{val:08X}\n")
    click.echo(f"  -> nboot_ddr_init.txt")


def extract_eboot(data: bytes, out_dir: Path, name: str, offset: int) -> None:
    """Extract eboot from Anyka IMG wrapper at given offset."""
    magic = data[offset : offset + 4]
    if magic != b"IMG\x00":
        click.echo(f"  WARNING: IMG magic not found at 0x{offset:X}")
        return

    img_type = data[offset + 4 : offset + 8].rstrip(b"\x00").decode("ascii")
    filename = data[offset + 8 : offset + 0x18].split(b"\x00")[0].decode("ascii")
    load_addr = struct.unpack_from("<I", data, offset + 0x18)[0]
    region_size = struct.unpack_from("<I", data, offset + 0x1C)[0]

    click.echo(f"  IMG type:        {img_type}")
    click.echo(f"  Filename:        {filename}")
    click.echo(f"  Load addr:       0x{load_addr:08X}")
    click.echo(f"  Region size:     0x{region_size:X} ({region_size // 1024} KB)")

    img_region = data[offset : offset + region_size]
    (out_dir / f"{name}.akimg").write_bytes(img_region)
    click.echo(f"  -> {name}.akimg ({len(img_region)} B)")

    # Strip Anyka IMG header; binary code starts at +0x2C
    bin_data = data[offset + 0x2C : offset + region_size]
    end = len(bin_data)
    while end > 0 and bin_data[end - 1] in (0x00, 0xFF):
        end -= 1
    end = (end + 3) & ~3
    bin_trimmed = bin_data[:end]

    (out_dir / f"{name}.nb0").write_bytes(bin_trimmed)
    click.echo(f"  -> {name}.nb0 ({len(bin_trimmed)} B)")


def extract_nk(data: bytes, out_dir: Path) -> None:
    """Extract the NK / WinCE OS region."""
    nk_start = 0x00480000
    block_size = 0x20000

    last_data_block = nk_start
    consecutive_empty = 0
    for block_start in range(nk_start, len(data), block_size):
        block = data[block_start : block_start + block_size]
        is_empty = all(b == 0xFF for b in block) or all(b == 0x00 for b in block)
        if not is_empty:
            last_data_block = block_start + block_size
            consecutive_empty = 0
        else:
            consecutive_empty += 1
            if consecutive_empty >= 16:
                break

    nk_size = last_data_block - nk_start
    click.echo(f"  Region:          0x{nk_start:08X}..0x{last_data_block:08X}")
    click.echo(f"  Size:            0x{nk_size:X} ({nk_size // (1024 * 1024)} MB)")

    nk_data = data[nk_start:last_data_block]
    (out_dir / "nk.raw").write_bytes(nk_data)
    click.echo(f"  -> nk.raw ({len(nk_data)} B)")

    if nk_data[:7] == b"B000FF\n":
        img_start = struct.unpack_from("<I", nk_data, 7)[0]
        img_len = struct.unpack_from("<I", nk_data, 11)[0]
        click.echo(f"  WinCE BIN header: start=0x{img_start:08X}, len=0x{img_len:X}")
    else:
        click.echo(f"  No WinCE BIN (B000FF) header; Anyka proprietary format")


def extract_ptb(data: bytes, out_dir: Path) -> None:
    """Extract the PTB (Partition Table Block)."""
    ptb_offset = 0x1FF60000
    if ptb_offset + 4 > len(data):
        click.echo("  PTB offset out of range")
        return
    magic = data[ptb_offset : ptb_offset + 4]
    if magic != b"PTB\x00":
        click.echo(f"  WARNING: PTB magic not found at 0x{ptb_offset:X}")
        return

    click.echo(f"  Offset:          0x{ptb_offset:08X}")
    ptb_data = data[ptb_offset : ptb_offset + 4096]
    (out_dir / "ptb.bin").write_bytes(ptb_data)
    click.echo(f"  -> ptb.bin ({len(ptb_data)} B)")


@click.command()
@click.argument("nand_image", type=click.Path(exists=True))
@click.option(
    "-o", "--output",
    type=click.Path(),
    default=None,
    help="Output directory. Default: <nand_image_dir>/extracted.",
)
def main(nand_image: str, output: str | None) -> None:
    """Extract boot images from an AIPC netbook NAND dump.

    Splits NAND_IMAGE into nboot, eboot, NK, and PTB components.
    """
    nand_path = Path(nand_image)
    out_dir = Path(output) if output else nand_path.parent / "extracted"

    click.echo(f"Reading {nand_path} ...")
    data = nand_path.read_bytes()
    click.echo(f"  Size: {len(data)} bytes ({len(data) // (1024 * 1024)} MB)")

    out_dir.mkdir(parents=True, exist_ok=True)
    click.echo(f"Output: {out_dir}\n")

    click.echo("=== nboot ===")
    extract_nboot(data, out_dir)

    click.echo("\n=== eboot (IPL) ===")
    extract_eboot(data, out_dir, "eboot", 0x00040000)

    click.echo("\n=== eboot (BAK) ===")
    extract_eboot(data, out_dir, "eboot_bak", 0x00240000)

    click.echo("\n=== NK ===")
    extract_nk(data, out_dir)

    click.echo("\n=== PTB ===")
    extract_ptb(data, out_dir)

    click.echo(f"\nDone. Files written to {out_dir}")
