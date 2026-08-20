"""Command-line interface for aipc-coldboot-dump."""

import time
from pathlib import Path

import click
import tqdm

from aipc_ddr_init import ddr_init
from ak7802_usbboot.transport import AK7802, find_device

DDR_BASE = 0x30000000
DDR_LENGTH = 0x04000000
POST_INIT_SETTLE_S = 0.1


def _dump_range(
    dev: AK7802,
    out_path: Path,
    base: int,
    length: int,
) -> None:
    with tqdm.tqdm(
        total=length,
        unit="B",
        unit_scale=True,
        unit_divisor=1024,
        desc="writing",
        leave=True,
    ) as bar:
        data = dev.read_mem(base, length, progress=bar.update)
    with open(out_path, "wb") as f:
        f.write(data)


@click.command()
@click.option(
    "--firmware",
    "firmware",
    required=True,
    type=click.Choice(("1.58.2", "1.88"), case_sensitive=True),
    help="Target firmware DDR init sequence.",
)
@click.option(
    "-o",
    "--output",
    "output_path",
    required=True,
    type=click.Path(dir_okay=False, path_type=Path),
    help="Dump output file.",
)
@click.option(
    "--stub",
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
    help="Path to a compiled DDR init stub binary.",
)
@click.option("-v", "--verbose", is_flag=True)
def main(
    firmware: str,
    output_path: Path,
    stub: Path | None,
    verbose: bool,
) -> None:
    """Wait for USB boot, initialize DDR, then dump 64 MiB of RAM."""
    if verbose:
        click.echo(f"firmware  {firmware}")
        click.echo(f"output    {output_path}")
        click.echo(f"range     {DDR_BASE:#010x}..{DDR_BASE + DDR_LENGTH - 1:#010x}")

    click.echo("Connecting...")
    dev = find_device(wait=True)

    click.echo("DDR init...")
    label, _ = ddr_init(dev, firmware=firmware, stub=stub)

    _dump_range(dev, output_path, DDR_BASE, DDR_LENGTH)

    click.echo(f"cold-boot dump saved to {output_path}")


if __name__ == "__main__":
    main()
