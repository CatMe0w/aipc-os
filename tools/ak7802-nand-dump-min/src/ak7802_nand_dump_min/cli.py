import struct
import sys
import time
from pathlib import Path

import click

from ak7802_usbboot.transport import find_device, AK7802

# Past the EP3 receive window at 0x48000200-0x4800023F.
STUB_ADDR = 0x48000240

# Must match stub/stub.c.
PARAM_ADDR = 0x48000040
DATA_ADDR = 0x48000400

CMD_HW_INIT = 0
CMD_PROBE_READ = 1

# The nf_probe_params table of the bootrom:
# (counts, command, timing_cfg0, timing_cfg1, delay_pair)
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

READ_STAGES = {
    0x10: "stopped in the hardware initialization",
    0x11: "stopped while setting the timing",
    0x12: "stopped in the command sequence",
    0x13: "stopped before the first chunk",
    0x14: "stopped in the first chunk",
}

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
        0,
    )


def _read_status(dev: AK7802) -> int:
    return struct.unpack("<I", dev.read_mem(PARAM_ADDR + 0x30, 4))[0]


def nand_hw_init(dev: AK7802) -> None:
    dev.write_mem(PARAM_ADDR, _pack_params(command=CMD_HW_INIT))
    dev.execute(STUB_ADDR, wait=True)
    status = _read_status(dev)
    if status != 0:
        raise RuntimeError(f"NAND hardware initialization failed (status {status})")


def nand_read_id(dev: AK7802) -> bytes:
    # One command (0x90), one address byte, the timing registers unchanged.
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
        # The stub writes a progress marker to the status word, see stub.c.
        stage = "unknown"
        try:
            time.sleep(0.5)
            marker = struct.unpack(
                "<I", find_device(wait=False).read_mem(PARAM_ADDR + 0x30, 4))[0]
            stage = READ_STAGES.get(marker, f"unknown marker {marker:#04x}")
        except Exception:
            stage = "the boot ROM does not respond"
        raise RuntimeError(f"Read ID: the stub did not return ({stage})")
    if status != 0:
        raise RuntimeError(f"Read ID failed (status {status:#04x})")
    return dev.read_mem(DATA_ADDR, 8)


def detect_geometry(id_bytes: bytes) -> dict | None:
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
    chunks_per_page = page_size // 512
    raw_page_size = chunks_per_page * 528

    return {
        "page_size": page_size,
        "block_size": block_size,
        "total_size": total_size,
        "addr_cycles": addr_cycles,
        "total_pages": total_pages,
        "large_page": large_page,
        "chunks_per_page": chunks_per_page,
        "raw_page_size": raw_page_size,
        "total_raw_size": raw_page_size * total_pages,
    }


def build_page_read_param(geo: dict) -> tuple[int, int, int, int, int]:
    if geo["large_page"]:
        row_cycles = geo["addr_cycles"] - 2
        prefix = 2
        cmd_count = 2
        cmd2 = 0x30
    else:
        row_cycles = geo["addr_cycles"] - 1
        prefix = 1
        cmd_count = 1
        cmd2 = 0x00

    counts = (cmd_count << 16) | (prefix << 24)
    command = row_cycles | (0x00 << 8) | (cmd2 << 16)
    delay_pair = 0x000A000A

    return (counts, command, 0, 0, delay_pair)


def probe_page0(dev: AK7802) -> tuple[int, int, int, int, int] | None:
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
                chunk_size=32,
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
        click.echo(f"Probe parameter set {i} succeeded.")
        return probe_param
    return None


def read_page(
    dev: AK7802,
    page: int,
    probe_param: tuple[int, int, int, int, int],
    chunks_per_page: int,
    raw_page_size: int,
    timing0: int = 0,
    timing1: int = 0,
    pre_delay: int = 0,
) -> bytes:
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
        raise RuntimeError(f"read failed (status {status})")
    return dev.read_mem(DATA_ADDR, raw_page_size)


@click.command()
@click.option(
    "--stub",
    type=click.Path(exists=True),
    default=None,
    help="Stub binary (default: stub/stub.bin).",
)
@click.option(
    "-o",
    "--output",
    required=True,
    type=click.Path(),
    help="Output file for the raw NAND image, spare bytes included.",
)
def main(stub: str | None, output: str) -> None:
    """Dump the raw contents of the NAND of an AK7802 device in USB boot mode."""

    if stub is not None:
        stub_path = Path(stub)
    else:
        stub_path = Path(__file__).resolve().parents[2] / "stub" / "stub.bin"
    if not stub_path.exists():
        click.echo(f"Error: {stub_path}: file not found. Build it with: make -C stub",
                   err=True)
        sys.exit(1)

    stub_data = stub_path.read_bytes()
    dev = find_device()
    click.echo("Device connected.")
    dev.write_mem(STUB_ADDR, stub_data)
    nand_hw_init(dev)

    id_bytes = nand_read_id(dev)
    mfr = id_bytes[0]
    click.echo(f"NAND ID: {id_bytes.hex()} "
               f"({KNOWN_MANUFACTURERS.get(mfr, 'unknown manufacturer')})")

    geo = detect_geometry(id_bytes)

    probe_param = None
    working_timing = (0, 0, 0)  # (timing0, timing1, pre_delay)

    if geo is not None:
        click.echo(
            f"Geometry: {geo['total_pages']} pages of {geo['page_size']} bytes "
            f"({geo['raw_page_size']} bytes raw), "
            f"{geo['block_size'] // 1024} KB blocks, "
            f"{geo['addr_cycles']} address cycles"
        )
        probe_param = build_page_read_param(geo)
    else:
        click.echo("Unknown NAND ID. Probing page 0 with the boot ROM parameter sets.")

        result = probe_page0(dev)
        if result is None:
            click.echo("Error: no probe parameter set could read page 0.", err=True)
            sys.exit(1)

        probe_param = result
        counts = probe_param[0]
        command = probe_param[1]
        chunks_per_page = counts & 0xFF
        page_size = chunks_per_page * 512
        addr_cycles = (command & 0xFF) + ((counts >> 24) & 0xFF)
        cmd_count = (counts >> 16) & 0xFF
        large_page = cmd_count == 2
        raw_page_size = chunks_per_page * 528

        click.echo(
            f"Geometry: pages of {page_size} bytes ({raw_page_size} bytes raw), "
            f"{addr_cycles} address cycles"
        )
        click.echo(
            "Warning: the capacity is unknown. The dump continues until reads fail.",
            err=True,
        )

        geo = {
            "page_size": page_size,
            "block_size": 0,
            "total_size": 0,
            "addr_cycles": addr_cycles,
            "total_pages": 0,
            "large_page": large_page,
            "chunks_per_page": chunks_per_page,
            "raw_page_size": raw_page_size,
            "total_raw_size": 0,
        }

        working_timing = (probe_param[2], probe_param[3], probe_param[4] & 0xFFFF)

    chunks_per_page = geo["chunks_per_page"]
    raw_page_size = geo["raw_page_size"]
    total_pages = geo["total_pages"]
    total_size = geo["total_raw_size"]

    if total_pages == 0:
        total_pages = 0xFFFFFFFF

    click.echo(f"Writing to {output}.")

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
                    raw_page_size,
                    timing0=working_timing[0],
                    timing1=working_timing[1],
                    pre_delay=working_timing[2],
                )
            except Exception as e:
                errors += 1
                if errors >= max_consecutive_errors:
                    # With an unknown capacity, this is how the dump ends. The
                    # fill pages of this run are past the end of the NAND.
                    filled = errors - 1
                    page -= filled
                    received -= filled * raw_page_size
                    f.truncate(received)
                    click.echo(
                        f"\nError: {max_consecutive_errors} consecutive read "
                        f"failures from page {page}; stopping.",
                        err=True,
                    )
                    break
                click.echo(f"\nWarning: page {page}: {e}. Filled with 0xFF.", err=True)
                f.write(b"\xff" * raw_page_size)
                received += raw_page_size
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

    click.echo(f"\nDump complete: {page} pages, {received} bytes.")
    if total_size and received != total_size:
        click.echo(
            f"Warning: {received} bytes written, expected {total_size}.",
            err=True,
        )
