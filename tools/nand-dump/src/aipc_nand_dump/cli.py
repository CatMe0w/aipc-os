"""
NAND flash dump tool for AIPC.

Flow:
  1. Connect to device in USB boot mode.
  2. Initialize DDR via the aipc-ddr-init stub.
  3. Upload nand_id stub, execute, read back NAND ID.
  4. Resolve NAND geometry (auto-detect or manual override).
  5. Upload nand_copy stub.
  6. Loop: write params -> execute (reads NAND batch to DDR) -> read_mem DDR.
  7. Write raw dump to file (including OOB/ECC data).

All USB communication uses the bootrom's native protocol (read_mem/write_mem/
execute). No more USB session hijacking!
"""

import struct
import sys
import time
from pathlib import Path

import click
import tqdm

from aipc_ddr_init.cli import ddr_init
from ak7802_usbboot.transport import AK7802, ExecuteTimeoutError, find_device

# Constants
STUB_ADDR = 0x48000240
PARAM_ADDR = 0x48000040
RESULT_ADDR = PARAM_ADDR  # result overwrites params after stub completes

DDR_BASE = 0x30000000
DDR_USABLE = 63 * 1024 * 1024  # 63 MB out of 64 MB

# Per-batch NAND read timeout.  30720 pages x ~100us/page ~ 3s, generous.
NAND_BATCH_TIMEOUT_S = 30.0

KNOWN_MANUFACTURERS = {
    0x2C: "Micron",
    0x45: "SanDisk",
    0x89: "Intel",
    0x98: "Toshiba/Kioxia",
    0xAD: "Hynix",
    0xC2: "Macronix",
    0xEC: "Samsung",
}

DEFAULT_FIRMWARE = "1.88"


# Stub helpers
def _default_stub_path(name: str) -> Path:
    return Path(__file__).resolve().parents[2] / "stub" / f"{name}.bin"


def _load_stub(name: str, stub_override: Path | None = None) -> bytes:
    path = stub_override if stub_override else _default_stub_path(name)
    if not path.exists():
        click.echo(f"Stub not found: {path}", err=True)
        click.echo("Build stubs first: cd stub && make", err=True)
        sys.exit(1)
    return path.read_bytes()


# NAND ID & timing
def nand_read_id(dev: AK7802, stub_data: bytes) -> tuple[bytes, int, int, int]:
    """Upload and execute the nand_id stub.

    Returns (id_bytes, timing_cfg0, timing_cfg1, hdr_delay).
    timing values are 0 if the NAND header was not found.
    """
    dev.write_mem(STUB_ADDR, stub_data)
    dev.execute(STUB_ADDR, wait=True, timeout=5.0)

    result = dev.read_mem(RESULT_ADDR, 24)
    status, id_word0, id_word1, timing0, timing1, hdr_delay = struct.unpack("<6I", result)
    if status != 0:
        click.echo("NAND Read ID failed", err=True)
        sys.exit(1)

    return struct.pack("<II", id_word0, id_word1), timing0, timing1, hdr_delay


# Geometry detection
def detect_geometry(id_bytes: bytes) -> dict | None:
    """Infer NAND geometry from the ONFI / JEDEC ID bytes."""
    dev_id = id_bytes[1]
    byte3 = id_bytes[3]

    capacity_table = {
        0x73: 16,
        0x75: 32,
        0x76: 64,
        0x79: 128,
        0xF1: 128,
        0xA1: 128,
        0xDA: 256,
        0xAA: 256,
        0xDC: 512,
        0xAC: 512,
        0xD3: 1024,
        0xA3: 1024,
        0xD5: 2048,
        0xA5: 2048,
        0xD7: 4096,
    }
    total_mb = capacity_table.get(dev_id)
    if total_mb is None:
        return None

    small_page_ids = {0x73, 0x75, 0x76, 0x79}
    if dev_id in small_page_ids:
        page_size = 512
        chunks_per_page = 1
        addr_cycles = 4 if total_mb > 32 else 3
    else:
        page_size = 1024 << (byte3 & 0x03)
        chunks_per_page = page_size // 512
        addr_cycles = 5 if total_mb > 128 else 4

    total_pages = (total_mb * 1024 * 1024) // page_size
    return {
        "page_size": page_size,
        "chunks_per_page": chunks_per_page,
        "addr_cycles": addr_cycles,
        "total_pages": total_pages,
    }


def resolve_geometry(
    id_bytes: bytes,
    pages: int | None,
    page_size: int | None,
    addr_cycles: int | None,
) -> dict:
    """Merge auto-detected geometry with user overrides."""
    geo = detect_geometry(id_bytes) or {}

    ps = page_size if page_size is not None else geo.get("page_size")
    ac = addr_cycles if addr_cycles is not None else geo.get("addr_cycles")
    tp = pages if pages is not None else geo.get("total_pages")

    if ps is None or ac is None or tp is None:
        click.echo(
            "Cannot determine NAND geometry from Read ID; provide " "--pages, --page-size, and --addr-cycles manually.",
            err=True,
        )
        sys.exit(1)
    if ps <= 0 or ps % 512 != 0:
        click.echo(f"Unsupported page size: {ps}", err=True)
        sys.exit(1)

    cpp = ps // 512
    raw_page_size = cpp * 528  # data + OOB interleaved

    return {
        "page_size": ps,
        "chunks_per_page": cpp,
        "addr_cycles": ac,
        "total_pages": tp,
        "raw_page_size": raw_page_size,
        "total_raw_bytes": raw_page_size * tp,
        "from_id": bool(geo),
    }


# Probe param builder
def build_probe_param(page_size: int, addr_cycles: int, delay_pair: int = 0) -> tuple[int, int, int, int, int]:
    """Build the 5-word nf_probe_param for page reads."""
    large_page = page_size > 512
    if large_page:
        row_cycles = addr_cycles - 2
        prefix = 2
        cmd_count = 2
        cmd2 = 0x30
    else:
        row_cycles = addr_cycles - 1
        prefix = 1
        cmd_count = 1
        cmd2 = 0x00

    counts = (cmd_count << 16) | (prefix << 24)
    command = row_cycles | (0x00 << 8) | (cmd2 << 16)
    if not delay_pair:
        delay_pair = 0x000A000A  # same as bootrom probe table
    return (counts, command, 0, 0, delay_pair)


# NAND copy (one batch)
def _pack_nand_copy_params(
    start_page: int,
    batch_pages: int,
    ddr_dest: int,
    chunks_per_page: int,
    probe_param: tuple[int, int, int, int, int],
    nfc_timing0: int,
    nfc_timing1: int,
) -> bytes:
    return struct.pack(
        "<11I",
        start_page,
        batch_pages,
        ddr_dest,
        chunks_per_page,
        *probe_param,
        nfc_timing0,
        nfc_timing1,
    )


def _unpack_nand_copy_result(data: bytes) -> tuple[int, int, int]:
    """Return (status, pages_done, error_count)."""
    return struct.unpack("<III", data[:12])


# CLI
class _HexInt(click.ParamType):
    name = "integer"

    def convert(self, value, param, ctx):
        if isinstance(value, int):
            return value
        try:
            return int(value, 0)
        except ValueError:
            self.fail(f"{value!r} is not a valid integer", param, ctx)


_INT = _HexInt()


@click.command()
@click.option(
    "-o",
    "--output",
    required=True,
    type=click.Path(),
    help="Output file for the raw NAND dump (data + OOB).",
)
@click.option(
    "--firmware",
    default=DEFAULT_FIRMWARE,
    type=click.Choice(["1.58.2", "1.88"], case_sensitive=True),
    help="DDR init firmware version.",
)
@click.option("--pages", type=int, default=None, help="Override total NAND page count.")
@click.option("--page-size", type=int, default=None, help="Override page size in bytes (data only, e.g. 2048).")
@click.option("--addr-cycles", type=int, default=None, help="Override NAND address cycle count.")
@click.option("--ddr-base", type=_INT, default=DDR_BASE, show_default=True, help="DDR buffer base address.")
def main(
    output: str,
    firmware: str,
    pages: int | None,
    page_size: int | None,
    addr_cycles: int | None,
    ddr_base: int,
) -> None:
    """Dump NAND flash contents via USB boot mode (raw, including OOB/ECC)."""

    nand_id_stub = _load_stub("nand_id")
    nand_copy_stub = _load_stub("nand_copy")

    # Connect
    click.echo("Connecting...")
    dev = find_device()

    # DDR init
    click.echo(f"DDR init (firmware {firmware})...")
    try:
        ddr_init(dev, firmware=firmware)
    except click.ClickException as e:
        click.echo(f"DDR init failed: {e.format_message()}", err=True)
        sys.exit(1)
    click.echo("DDR init done")

    # NAND ID & timing
    click.echo("Reading NAND ID & header timing...")
    id_bytes, timing_cfg0, timing_cfg1, hdr_delay = nand_read_id(dev, nand_id_stub)
    mfr = id_bytes[0]
    click.echo(f"  ID:           {id_bytes.hex()}")
    click.echo(f"  Manufacturer: 0x{mfr:02X} ({KNOWN_MANUFACTURERS.get(mfr, 'Unknown')})")
    click.echo(f"  Device ID:    0x{id_bytes[1]:02X}")
    if timing_cfg0 or timing_cfg1:
        click.echo(f"  NFC timing:   0x{timing_cfg0:05X} / 0x{timing_cfg1:05X}")
        click.echo(f"  Header delay: 0x{hdr_delay:08X}")
    else:
        click.echo("  NFC timing:   default (header not found)")

    # Geometry
    geo = resolve_geometry(id_bytes, pages, page_size, addr_cycles)
    rps = geo["raw_page_size"]
    cpp = geo["chunks_per_page"]
    total_pages = geo["total_pages"]
    total_raw = geo["total_raw_bytes"]

    click.echo(f"  Page size:    {geo['page_size']} B (raw: {rps} B)")
    click.echo(f"  Addr cycles:  {geo['addr_cycles']}")
    click.echo(f"  Total pages:  {total_pages}")
    click.echo(f"  Total size:   {total_pages * geo['page_size'] // (1024*1024)} MB " f"(raw: {total_raw / (1024*1024):.1f} MB)")
    click.echo(f"  Geometry:     {'auto-detected' if geo['from_id'] else 'manual override'}")

    # Prepare
    probe_param = build_probe_param(geo["page_size"], geo["addr_cycles"], delay_pair=hdr_delay)
    batch_pages = DDR_USABLE // rps
    num_batches = (total_pages + batch_pages - 1) // batch_pages

    click.echo(
        f"Dumping {total_raw / (1024*1024):.1f} MiB raw in "
        f"{num_batches} batch(es), {batch_pages} pages/batch..."
    )

    # Upload nand_copy stub once; it stays in L2 SRAM across batches
    dev.write_mem(STUB_ADDR, nand_copy_stub)

    total_errors = 0
    received = 0

    with open(output, "wb") as f:
        with tqdm.tqdm(
            total=total_raw,
            unit="B",
            unit_scale=True,
            unit_divisor=1024,
            desc="dumping",
            leave=True,
        ) as bar:
            for batch_idx in range(num_batches):
                start = batch_idx * batch_pages
                this_batch = min(batch_pages, total_pages - start)
                batch_bytes = this_batch * rps
                batch_label = f"{batch_idx+1}/{num_batches}"

                # Write params and execute
                params = _pack_nand_copy_params(
                    start,
                    this_batch,
                    ddr_base,
                    cpp,
                    probe_param,
                    timing_cfg0,
                    timing_cfg1,
                )
                dev.write_mem(PARAM_ADDR, params)

                # Device-side read. The byte progress bar advances during transfer.
                bar.set_description("reading", refresh=True)
                tqdm.tqdm.write(f"  batch {batch_label}: reading {this_batch} pages...")
                timeout = max(NAND_BATCH_TIMEOUT_S, this_batch * 0.05)
                t0 = time.monotonic()

                try:
                    dev.execute(STUB_ADDR, wait=True, timeout=timeout)
                except ExecuteTimeoutError:
                    tqdm.tqdm.write(f"ERROR: batch {batch_label} timed out " f"(pages {start}-{start+this_batch-1})")
                    sys.exit(1)

                elapsed = time.monotonic() - t0

                # Check result
                result_raw = dev.read_mem(RESULT_ADDR, 12)
                status, pages_done, error_count = _unpack_nand_copy_result(result_raw)

                if error_count > 0:
                    total_errors += error_count
                    tqdm.tqdm.write(f"WARNING: {error_count} read error(s) in batch " f"{batch_label} " f"(pages {start}-{start+this_batch-1})")

                if pages_done != this_batch:
                    tqdm.tqdm.write(f"WARNING: batch {batch_idx+1} returned {pages_done} " f"pages, expected {this_batch}")

                # Transfer back to the host
                bar.set_description("transferring", refresh=True)
                transfer_t0 = time.monotonic()
                data = dev.read_mem(ddr_base, batch_bytes, progress=bar.update)
                transfer_elapsed = time.monotonic() - transfer_t0

                f.write(data)
                received += len(data)
                tqdm.tqdm.write(
                    f"  batch {batch_label} done: read {elapsed:.2f}s "
                    f"({elapsed/this_batch*1000:.1f} ms/page), "
                    f"transfer {transfer_elapsed:.2f}s, errors {error_count}"
                )

    click.echo(f"\nDone. {received:,} bytes -> {output}")
    if total_errors > 0:
        click.echo(f"  {total_errors} page(s) had read errors (filled with 0xFF)")


if __name__ == "__main__":
    main()
