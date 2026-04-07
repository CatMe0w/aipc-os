"""
Host-side NAND dump tool for AK7802.

Uploads a bare-metal stub to the device via USB boot mode, then receives
a full NAND flash dump over the same USB connection.
"""

import struct
import sys
from importlib.resources import files
from pathlib import Path

import click
import usb.core

from ak7802_usbboot.transport import find_device

EP_BULK_IN = 0x82
STUB_LOAD_ADDR = 0x48000200
HEADER_MAGIC = 0x444E414E  # "NAND" in little-endian


def _parse_header(data: bytes) -> dict:
    """Parse the 64-byte info header sent by the stub."""
    if len(data) < 64:
        raise ValueError(f"short header: {len(data)} bytes")
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != HEADER_MAGIC:
        raise ValueError(
            f"bad header magic: 0x{magic:08X} (expected 0x{HEADER_MAGIC:08X})\n"
            f"raw: {data.hex()}"
        )
    id_bytes = data[4:12]
    page_size, block_size, total_size, addr_cycles, total_pages, flags = (
        struct.unpack_from("<IIIIII", data, 12)
    )
    return {
        "id_bytes": id_bytes,
        "page_size": page_size,
        "block_size": block_size,
        "total_size": total_size,
        "addr_cycles": addr_cycles,
        "total_pages": total_pages,
        "flags": flags,
    }


@click.command()
@click.option(
    "--stub",
    type=click.Path(exists=True),
    default=None,
    help="Path to compiled stub binary.  Default: stub/stub.bin next to this package.",
)
@click.option(
    "-o", "--output",
    required=True,
    type=click.Path(),
    help="Output file for the NAND dump.",
)
@click.option(
    "--timeout",
    default=5000,
    show_default=True,
    help="USB read timeout per packet (ms).",
)
def main(stub: str | None, output: str, timeout: int) -> None:
    """Dump NAND flash contents via AK7802 USB boot mode."""

    # Locate stub binary
    if stub is not None:
        stub_path = Path(stub)
    else:
        # Default: look for stub.bin relative to the project layout
        stub_path = Path(__file__).resolve().parents[2] / "stub" / "stub.bin"

    if not stub_path.exists():
        click.echo(f"Stub binary not found: {stub_path}", err=True)
        click.echo("Build it first:  cd stub && make", err=True)
        sys.exit(1)

    stub_data = stub_path.read_bytes()
    click.echo(f"Stub: {stub_path} ({len(stub_data)} bytes)")

    # Connect to device
    click.echo("Connecting to device...")
    dev = find_device()

    # Upload and execute stub
    click.echo(f"Uploading stub to 0x{STUB_LOAD_ADDR:08X}...")
    dev.write_mem(STUB_LOAD_ADDR, stub_data)

    click.echo("Executing stub...")
    dev.execute(STUB_LOAD_ADDR)

    # After execute() the bootrom branches to the stub.  The USB
    # hardware state (FADDR, endpoint config) is preserved; the host-side
    # pyusb device handle remains valid for bulk reads.
    raw = dev._dev

    # Receive info header
    click.echo("Waiting for NAND info header...")
    try:
        header_raw = bytes(raw.read(EP_BULK_IN, 64, timeout=10000))
    except usb.core.USBTimeoutError:
        click.echo("Timeout waiting for header.  Stub may have failed to start.", err=True)
        sys.exit(1)

    try:
        hdr = _parse_header(header_raw)
    except ValueError as e:
        click.echo(str(e), err=True)
        sys.exit(1)

    id_bytes = hdr["id_bytes"]
    click.echo(f"NAND ID:       {id_bytes.hex()}")
    click.echo(f"  Manufacturer:  0x{id_bytes[0]:02X}")
    click.echo(f"  Device ID:     0x{id_bytes[1]:02X}")
    click.echo(f"  Page size:     {hdr['page_size']} B")
    click.echo(f"  Block size:    {hdr['block_size'] // 1024} KB")
    click.echo(f"  Total size:    {hdr['total_size'] // (1024 * 1024)} MB")
    click.echo(f"  Total pages:   {hdr['total_pages']}")
    click.echo(f"  Addr cycles:   {hdr['addr_cycles']}")
    click.echo(f"  Auto-detect:   {'OK' if hdr['flags'] & 1 else 'FAILED'}")

    if not (hdr["flags"] & 1):
        click.echo(
            "Auto-detection failed.  The stub sent only the header.\n"
            "Check the raw ID bytes above and verify NAND connectivity.",
            err=True,
        )
        sys.exit(1)

    total_size = hdr["total_size"]

    # Receive NAND data
    click.echo(f"\nDumping {total_size // (1024 * 1024)} MB to {output} ...")

    received = 0
    with open(output, "wb") as f:
        with click.progressbar(length=total_size, label="Reading") as bar:
            while received < total_size:
                try:
                    chunk = bytes(raw.read(EP_BULK_IN, 64, timeout=timeout))
                except usb.core.USBTimeoutError:
                    click.echo(
                        f"\nUSB timeout at offset 0x{received:X} "
                        f"({received * 100 // total_size}%)",
                        err=True,
                    )
                    break
                if len(chunk) == 0:
                    break
                f.write(chunk)
                received += len(chunk)
                bar.update(len(chunk))

    # Drain trailing ZLP
    try:
        raw.read(EP_BULK_IN, 64, timeout=500)
    except Exception:
        pass

    click.echo(f"\nDone.  Received {received} bytes ({received // (1024 * 1024)} MB)")
    if received != total_size:
        click.echo(
            f"WARNING: expected {total_size} bytes, got {received}",
            err=True,
        )
