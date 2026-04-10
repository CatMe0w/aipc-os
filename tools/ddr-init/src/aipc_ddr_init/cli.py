"""Command-line interface for aipc-ddr-init."""

from pathlib import Path

import click

from ak7802_usbboot.transport import ExecuteTimeoutError, find_device

STUB_ADDR = 0x48000240

_FIRMWARE_STUBS = {
    "1.58.2": ("v1.58.2", "ddr_init_v1_58_2.bin"),
    "1.88": ("v1.88", "ddr_init_v1_88.bin"),
}


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


def _default_stub_path(firmware: str) -> Path:
    _, filename = _FIRMWARE_STUBS[firmware]
    return Path(__file__).resolve().parents[2] / "stub" / filename


@click.command()
@click.option(
    "--firmware",
    "firmware",
    required=True,
    type=click.Choice(tuple(_FIRMWARE_STUBS.keys()), case_sensitive=True),
    help="Target firmware DDR init sequence.",
)
@click.option(
    "--stub",
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
    help="Path to a compiled stub binary. Default: version-matched stub in stub/.",
)
@click.option(
    "--addr",
    default=STUB_ADDR,
    show_default=True,
    type=_INT,
    metavar="ADDR",
    help="L2 SRAM upload address (decimal or 0x hex).",
)
@click.option("-v", "--verbose", is_flag=True)
def main(firmware: str, stub: Path | None, addr: int, verbose: bool) -> None:
    """Upload and execute a DDR init stub, then return to USB boot mode."""
    label, _ = _FIRMWARE_STUBS[firmware]
    stub_path = stub if stub is not None else _default_stub_path(firmware)

    if not stub_path.exists():
        raise click.ClickException(f"stub not found: {stub_path}\n" "Build it first with: cd tools/ddr-init/stub && make")

    data = stub_path.read_bytes()
    if not data:
        raise click.ClickException(f"stub is empty: {stub_path}")

    if verbose:
        click.echo(f"firmware  {label}")
        click.echo(f"stub      {stub_path}")
        click.echo(f"addr      {addr:#010x}")
        click.echo(f"size      {len(data):#x} bytes")

    dev = find_device(wait=True)
    dev.write_mem(addr, data)

    try:
        dev.execute(addr, wait=True)
    except ExecuteTimeoutError as exc:
        raise click.ClickException(f"DDR init stub at {addr:#010x} did not return to USB boot mode") from exc

    click.echo(f"DDR init for firmware {label} completed")


if __name__ == "__main__":
    main()
