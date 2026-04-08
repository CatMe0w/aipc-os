"""
Command-line interface for ak7802-usbboot.
"""

import sys

import click
import tqdm

from .transport import AK7802, DeviceNotFoundError, find_device


class _HexInt(click.ParamType):
    """Click parameter type that accepts decimal and 0x-prefixed hex integers."""

    name = "integer"

    def convert(self, value, param, ctx):
        if isinstance(value, int):
            return value
        try:
            return int(value, 0)
        except ValueError:
            self.fail(f"{value!r} is not a valid integer (use decimal or 0x hex)", param, ctx)


_INT = _HexInt()


def _get_device() -> AK7802:
    """Obtain a device handle, waiting if necessary."""
    return find_device(wait=True)


# ---------------------------------------------------------------------------
# Command group
# ---------------------------------------------------------------------------

@click.group()
def main() -> None:
    """USB boot tool for Anyka AK7802."""


# ---------------------------------------------------------------------------
# devices
# ---------------------------------------------------------------------------

@main.command()
def devices() -> None:
    """List connected AK7802 devices."""
    import usb.core
    from .protocol import VID, PID

    devs = list(usb.core.find(idVendor=VID, idProduct=PID, find_all=True))
    if not devs:
        click.echo("No AK7802 devices found.")
        click.echo("Pull DGPIO[2] high to enter USB boot mode.")
        sys.exit(1)
    for dev in devs:
        click.echo(
            f"AK7802  {VID:#06x}:{PID:#06x}"
            f"  bus {dev.bus:03d}  device {dev.address:03d}"
        )


# ---------------------------------------------------------------------------
# write
# ---------------------------------------------------------------------------

@main.command()
@click.argument("file", type=click.Path(exists=True, dir_okay=False))
@click.option("--addr", required=True, type=_INT, metavar="ADDR",
              help="Target RAM address (decimal or 0x hex).")
@click.option("-v", "--verbose", is_flag=True)
def write(file: str, addr: int, verbose: bool) -> None:
    """Write a binary file to device RAM at ADDR."""
    with open(file, "rb") as f:
        data = f.read()

    if verbose:
        click.echo(f"write  file={file}  addr={addr:#010x}  size={len(data):#x} bytes")

    dev = _get_device()

    with tqdm.tqdm(
        total=len(data), unit="B", unit_scale=True, unit_divisor=1024,
        desc="writing", leave=True,
    ) as bar:
        dev.write_mem(addr, data, progress=bar.update)

    click.echo(f"wrote {len(data):#x} bytes to {addr:#010x}")


# ---------------------------------------------------------------------------
# read
# ---------------------------------------------------------------------------

@main.command()
@click.option("--addr", required=True, type=_INT, metavar="ADDR",
              help="Source RAM address (decimal or 0x hex).")
@click.option("--len", "length", required=True, type=_INT, metavar="LEN",
              help="Number of bytes to read (decimal or 0x hex).")
@click.argument("file", type=click.Path(dir_okay=False))
@click.option("-v", "--verbose", is_flag=True)
def read(addr: int, length: int, file: str, verbose: bool) -> None:
    """Read LEN bytes from device RAM at ADDR and save to FILE."""
    if verbose:
        click.echo(f"read  addr={addr:#010x}  len={length:#x}  file={file}")

    dev = _get_device()

    with tqdm.tqdm(
        total=length, unit="B", unit_scale=True, unit_divisor=1024,
        desc="reading", leave=True,
    ) as bar:
        data = dev.read_mem(addr, length, progress=bar.update)

    with open(file, "wb") as f:
        f.write(data)

    click.echo(f"read {len(data):#x} bytes from {addr:#010x} → {file}")


# ---------------------------------------------------------------------------
# exec
# ---------------------------------------------------------------------------

@main.command(name="exec")
@click.option("--addr", required=True, type=_INT, metavar="ADDR",
              help="Address to execute (decimal or 0x hex).")
@click.option("--wait/--no-wait", default=False,
              help="Wait for a returning stub to resume USB boot mode.")
@click.option("-v", "--verbose", is_flag=True)
def exec_cmd(addr: int, wait: bool, verbose: bool) -> None:
    """Jump to ADDR on the device."""
    if verbose:
        click.echo(f"exec  addr={addr:#010x}  wait={wait}")

    dev = _get_device()
    dev.execute(addr, wait=wait)
    if wait:
        click.echo(f"executed at {addr:#010x}; USB boot resumed")
    else:
        click.echo(f"executing at {addr:#010x}")


# ---------------------------------------------------------------------------
# poke
# ---------------------------------------------------------------------------

@main.command()
@click.option("--addr", required=True, type=_INT, metavar="ADDR",
              help="Device address to write (decimal or 0x hex).")
@click.option("--value", required=True, type=_INT, metavar="VALUE",
              help="32-bit value to write (decimal or 0x hex).")
@click.option("-v", "--verbose", is_flag=True)
def poke(addr: int, value: int, verbose: bool) -> None:
    """Write a 32-bit VALUE to a device register/address at ADDR."""
    if verbose:
        click.echo(f"poke  addr={addr:#010x}  value={value:#010x}")

    dev = _get_device()
    dev.poke(addr, value)
    click.echo(f"wrote {value:#010x} → {addr:#010x}")
