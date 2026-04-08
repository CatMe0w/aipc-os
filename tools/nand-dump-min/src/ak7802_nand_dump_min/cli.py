"""
Minimal host-driven NAND dump reference implementation for AK7802.

Uploads a tiny stub that reads one NAND page per EXECUTE call, then
pulls the data back via the bootrom's read command.  All logic
(init, ID detection, geometry, page iteration) lives here on the host.
The stub never touches USB, avoiding L2BUF_00 / DMA conflicts.

Data flow per page:
  host: write_mem(params)   -> bootrom writes params to SRAM
  host: execute(stub)       -> stub runs: ROM helpers read NAND -> SRAM
                            <- stub returns to bootrom
  host: read_mem(data)      -> bootrom uploads SRAM contents to host
"""

import struct
import sys
from pathlib import Path

import click

from ak7802_usbboot.transport import find_device, AK7802

# Stub is loaded here (past the EP3 RX DMA zone at 0x200-0x23F)
STUB_ADDR = 0x48000240

# Parameter block: host writes before each EXECUTE
PARAM_ADDR = 0x48000040
PARAM_SIZE = 0x34  # 13 × 4 bytes

# Data output: host reads after each EXECUTE
DATA_ADDR = 0x48000400

# Stub commands
CMD_HW_INIT = 0
CMD_PROBE_READ = 1

# 8 probe parameter sets, identical to the bootrom's nf_probe_params table.
# Each entry: (counts, command, timing_cfg0, timing_cfg1, delay_pair)
PROBE_PARAMS = [
    (0x01010101, 0x00000004, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01020101, 0x00300004, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01010101, 0x00000003, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01020101, 0x00300003, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01010101, 0x00000002, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01020101, 0x00300002, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01010101, 0x00000005, 0x000C3671, 0x000D3637, 0x000A000A),
    (0x01020101, 0x00300005, 0x000C3671, 0x000D3637, 0x000A000A),
]

KNOWN_MANUFACTURERS = {
    0x2C: "Micron",
    0x45: "SanDisk",
    0x89: "Intel",
    0x98: "Toshiba/Kioxia",
    0xAD: "Hynix",
    0xC2: "Macronix",
    0xEC: "Samsung",
}


def _pack_params(
    command: int,
    probe_param: tuple[int, int, int, int, int] = (0, 0, 0, 0, 0),
    page: int = 0,
    chunks: int = 0,
    chunk_size: int = 0,
    timing0: int = 0,
    timing1: int = 0,
    pre_delay: int = 0,
) -> bytes:
    """Pack the parameter block for the stub."""
    return struct.pack(
        "<I5IIIIIIII",
        command,
        *probe_param,
        page,
        chunks,
        chunk_size,
        timing0,
        timing1,
        pre_delay,
        0,  # status (will be written by stub)
    )


def _read_status(dev: AK7802) -> int:
    """Read the status word written by the stub."""
    return struct.unpack("<I", dev.read_mem(PARAM_ADDR + 0x30, 4))[0]


def nand_hw_init(dev: AK7802) -> None:
    """Call the bootrom's nf_boot_hw_init via the stub."""
    dev.write_mem(PARAM_ADDR, _pack_params(command=CMD_HW_INIT))
    dev.execute(STUB_ADDR, wait=True)
    status = _read_status(dev)
    if status != 0:
        raise RuntimeError(f"hw_init failed: status={status}")


def nand_read_id(dev: AK7802) -> bytes:
    """Issue NAND Read ID (cmd 0x90, addr 0x00) and return 8 ID bytes."""
    # probe_param for Read ID:
    #   counts:  cmd_count=1, others=0 -> 0x00010000
    #   command: addr_byte_count=1, cmd1=0x90 -> 0x00009001
    #   timing:  0 (use whatever is currently set)
    #   delay:   seq_wait=10 ticks -> 0x000A0000
    id_param = (0x00010000, 0x00009001, 0, 0, 0x000A0000)
    dev.write_mem(
        PARAM_ADDR,
        _pack_params(
            command=CMD_PROBE_READ,
            probe_param=id_param,
            page=0,
            chunks=1,
            chunk_size=8,
        ),
    )
    dev.execute(STUB_ADDR, wait=True)
    try:
        status = _read_status(dev)
    except Exception:
        # Stub likely hung. Try to read status marker to see where.
        click.echo("\nread_id: USB timeout. Attempting to read status marker...", err=True)
        try:
            import time

            time.sleep(0.5)
            # Device might need a reset. Try re-finding it.
            dev2 = find_device(wait=False)
            marker = struct.unpack("<I", dev2.read_mem(PARAM_ADDR + 0x30, 4))[0]
            click.echo(f"  status marker: 0x{marker:02X}", err=True)
            if marker == 0x10:
                click.echo("  -> hung before or during hw_init", err=True)
            elif marker == 0x11:
                click.echo("  -> hw_init done, hung in timings/delay", err=True)
            elif marker == 0x12:
                click.echo("  -> timings done, hung in nf_issue_probe_sequence", err=True)
            elif marker == 0x13:
                click.echo("  -> probe done, hung in read_chunk_to_buf", err=True)
            elif marker == 0x14:
                click.echo("  -> entering chunk loop, hung in first read_chunk_to_buf", err=True)
            else:
                click.echo(f"  -> unknown marker state", err=True)
        except Exception as e2:
            click.echo(f"  Could not read marker: {e2}", err=True)
            click.echo("  Stub is completely hung (bootrom not responding).", err=True)
        raise RuntimeError("read_id hung: stub did not return from EXECUTE")
    if status != 0:
        raise RuntimeError(f"read_id failed: status=0x{status:02X}")
    return dev.read_mem(DATA_ADDR, 8)


def detect_geometry(id_bytes: bytes) -> dict | None:
    """Detect NAND geometry from Read ID bytes. Returns None if unknown."""
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
        block_size = 16384
        large_page = False
        addr_cycles = 4 if total_mb > 32 else 3
    else:
        page_size = 1024 << (byte3 & 0x03)
        block_size = 65536 << ((byte3 >> 4) & 0x03)
        large_page = True
        addr_cycles = 5 if total_mb > 128 else 4

    total_size = total_mb * 1024 * 1024
    total_pages = total_size // page_size

    return {
        "page_size": page_size,
        "block_size": block_size,
        "total_size": total_size,
        "addr_cycles": addr_cycles,
        "total_pages": total_pages,
        "large_page": large_page,
        "chunks_per_page": page_size // 512,
    }


def build_page_read_param(geo: dict) -> tuple[int, int, int, int, int]:
    """Build a probe_param tuple for reading pages based on geometry."""
    if geo["large_page"]:
        row_cycles = geo["addr_cycles"] - 2
        prefix = 2  # 2 column address bytes (zeroed by prefix mechanism)
        cmd_count = 2
        cmd2 = 0x30
    else:
        row_cycles = geo["addr_cycles"] - 1
        prefix = 1  # 1 column address byte
        cmd_count = 1
        cmd2 = 0x00

    counts = (cmd_count << 16) | (prefix << 24)
    command = row_cycles | (0x00 << 8) | (cmd2 << 16)
    delay_pair = 2000 << 16  # generous seq_wait

    return (counts, command, 0, 0, delay_pair)


def probe_page0(dev: AK7802) -> tuple[int, int, int, int, int] | None:
    """Try all 8 bootrom probe parameter sets to read page 0.

    Returns the first probe_param tuple that produces non-0xFF data,
    or None if all fail.
    """
    for i, (counts, command, t0, t1, delay) in enumerate(PROBE_PARAMS):
        probe_param = (counts, command, t0, t1, delay)
        pre_delay = delay & 0xFFFF
        dev.write_mem(
            PARAM_ADDR,
            _pack_params(
                command=CMD_PROBE_READ,
                probe_param=probe_param,
                page=0,
                chunks=1,
                chunk_size=32,  # just read first 32 bytes
                timing0=t0,
                timing1=t1,
                pre_delay=pre_delay,
            ),
        )
        dev.execute(STUB_ADDR, wait=True)
        status = _read_status(dev)
        if status != 0:
            continue
        data = dev.read_mem(DATA_ADDR, 32)
        if data == b"\xff" * 32:
            continue
        click.echo(f"  Probe set {i} succeeded: {data[:16].hex()}...")
        return probe_param
    return None


def read_page(
    dev: AK7802,
    page: int,
    probe_param: tuple[int, int, int, int, int],
    chunks_per_page: int,
    timing0: int = 0,
    timing1: int = 0,
    pre_delay: int = 0,
) -> bytes:
    """Read one full NAND page via the stub."""
    dev.write_mem(
        PARAM_ADDR,
        _pack_params(
            command=CMD_PROBE_READ,
            probe_param=probe_param,
            page=page,
            chunks=chunks_per_page,
            chunk_size=512,
            timing0=timing0,
            timing1=timing1,
            pre_delay=pre_delay,
        ),
    )
    dev.execute(STUB_ADDR, wait=True)
    status = _read_status(dev)
    if status != 0:
        raise RuntimeError(f"read_page({page}) failed: status={status}")
    return dev.read_mem(DATA_ADDR, chunks_per_page * 512)


@click.command()
@click.option(
    "--stub",
    type=click.Path(exists=True),
    default=None,
    help="Path to compiled stub binary.",
)
@click.option(
    "-o",
    "--output",
    required=True,
    type=click.Path(),
    help="Output file for the NAND dump.",
)
def main(stub: str | None, output: str) -> None:
    """Dump NAND flash contents via AK7802 USB boot mode (host-driven)."""

    # Locate stub
    if stub is not None:
        stub_path = Path(stub)
    else:
        stub_path = Path(__file__).resolve().parents[2] / "stub" / "stub.bin"
    if not stub_path.exists():
        click.echo(f"Stub not found: {stub_path}", err=True)
        click.echo("Build it:  cd stub && make", err=True)
        sys.exit(1)

    stub_data = stub_path.read_bytes()
    click.echo(f"Stub: {stub_path} ({len(stub_data)} bytes)")

    # Connect
    click.echo("Connecting...")
    dev = find_device()

    # Upload stub (once)
    click.echo(f"Uploading stub to 0x{STUB_ADDR:08X}...")
    dev.write_mem(STUB_ADDR, stub_data)

    # NAND hardware init
    click.echo("NAND hw_init...")
    nand_hw_init(dev)
    click.echo("  OK")

    # Read ID
    click.echo("Reading NAND ID...")
    id_bytes = nand_read_id(dev)
    click.echo(f"  Raw ID: {id_bytes.hex()}")

    mfr = id_bytes[0]
    mfr_name = KNOWN_MANUFACTURERS.get(mfr, "Unknown")
    click.echo(f"  Manufacturer: 0x{mfr:02X} ({mfr_name})")
    click.echo(f"  Device ID:    0x{id_bytes[1]:02X}")

    # Detect geometry
    geo = detect_geometry(id_bytes)

    # If ID-based detection fails, try probing page 0
    probe_param = None
    working_timing = (0, 0, 0)  # (timing0, timing1, pre_delay)

    if geo is not None:
        click.echo(
            f"  Page size:    {geo['page_size']} B\n"
            f"  Block size:   {geo['block_size'] // 1024} KB\n"
            f"  Total size:   {geo['total_size'] // (1024 * 1024)} MB\n"
            f"  Total pages:  {geo['total_pages']}\n"
            f"  Addr cycles:  {geo['addr_cycles']}\n"
            f"  Large page:   {geo['large_page']}"
        )
        probe_param = build_page_read_param(geo)
    else:
        click.echo("  ID-based geometry detection failed.")
        click.echo("  Probing page 0 with bootrom parameter sets...")

        result = probe_page0(dev)
        if result is None:
            click.echo("All 8 probe sets failed. Cannot read NAND.", err=True)
            sys.exit(1)

        probe_param = result
        counts = probe_param[0]
        command = probe_param[1]
        chunks_per_page = counts & 0xFF
        page_size = chunks_per_page * 512
        addr_cycles = (command & 0xFF) + ((counts >> 24) & 0xFF)
        cmd_count = (counts >> 16) & 0xFF
        large_page = cmd_count == 2

        click.echo(f"  Page size:    {page_size} B (from probe)\n" f"  Addr cycles:  {addr_cycles}\n" f"  Large page:   {large_page}")
        click.echo(
            "WARNING: total size unknown from probe. " "Dump will continue until read errors.",
            err=True,
        )

        geo = {
            "page_size": page_size,
            "block_size": 0,
            "total_size": 0,
            "addr_cycles": addr_cycles,
            "total_pages": 0,  # unknown
            "large_page": large_page,
            "chunks_per_page": chunks_per_page,
        }

        # Use the working timing from the successful probe
        working_timing = (
            probe_param[2],  # timing_cfg0
            probe_param[3],  # timing_cfg1
            probe_param[4] & 0xFFFF,  # pre_delay
        )

    chunks_per_page = geo["chunks_per_page"]
    total_pages = geo["total_pages"]
    total_size = geo["total_size"]

    if total_pages == 0:
        click.echo(
            "Total page count unknown. Use --pages to specify, or press Ctrl-C to stop.",
            err=True,
        )
        total_pages = 0xFFFFFFFF  # read until error

    # Begin dump
    page_size = geo["page_size"]
    click.echo(f"\nDumping to {output} ...")

    received = 0
    errors = 0
    max_consecutive_errors = 16

    with open(output, "wb") as f:
        page = 0
        while page < total_pages:
            try:
                data = read_page(
                    dev,
                    page,
                    probe_param,
                    chunks_per_page,
                    timing0=working_timing[0],
                    timing1=working_timing[1],
                    pre_delay=working_timing[2],
                )
            except Exception as e:
                errors += 1
                if errors >= max_consecutive_errors:
                    click.echo(
                        f"\n{max_consecutive_errors} consecutive errors at page {page}. Stopping.",
                        err=True,
                    )
                    break
                click.echo(f"\nError at page {page}: {e}", err=True)
                page += 1
                continue

            errors = 0
            f.write(data)
            received += len(data)
            page += 1

            if page % 256 == 0:
                if total_size:
                    pct = received * 100 // total_size
                    click.echo(
                        f"\r  {received // 1024} KB / " f"{total_size // 1024} KB ({pct}%) " f"page {page}/{total_pages}",
                        nl=False,
                    )
                else:
                    click.echo(
                        f"\r  {received // 1024} KB, page {page}",
                        nl=False,
                    )

    click.echo(f"\n\nDone. {received} bytes ({received // (1024 * 1024)} MB), {page} pages.")
    if total_size and received != total_size:
        click.echo(
            f"WARNING: expected {total_size} bytes, got {received}",
            err=True,
        )
