"""Command-line interface for openNBOOT."""

from __future__ import annotations

import sys
from pathlib import Path

import click

from . import install as install_mod
from . import log as log_mod
from . import restore as restore_mod
from . import run as run_mod
from .images import ImageError
from .nand import NandError


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

# Only useful against a scratch block, so it stays out of --help.
_row = click.option("--row", type=int, default=0, hidden=True)


@click.group()
@click.version_option(package_name="opennboot")
def cli() -> None:
    """openNBOOT, a replacement bootloader for the AIPC netbook.

    Put the device into USB boot mode before you run any of these commands.
    """


@cli.command()
@_row
@click.pass_context
def install(ctx: click.Context, row: int) -> None:
    """Install openNBOOT into NAND block 0."""
    ctx.exit(install_mod.run(row=row))


@cli.command()
@click.option("--image", "image", required=True,
              type=click.Path(exists=True, dir_okay=False, path_type=Path),
              help="the backup_nboot-*.bin file `install` saved")
@_row
@click.pass_context
def restore(ctx: click.Context, image: Path, row: int) -> None:
    """Put the stock bootloader back."""
    ctx.exit(restore_mod.run(image, row=row))


@cli.command()
@click.option("--addr", type=_INT, default=run_mod.LOAD_ADDR, hidden=True)
@click.pass_context
def run(ctx: click.Context, addr: int) -> None:
    """Run openNBOOT from memory, without writing to NAND."""
    ctx.exit(run_mod.run(addr=addr))


@cli.command()
@click.option("--slot", type=click.Choice(list(log_mod.SLOTS)),
              default=log_mod.DEFAULT_SLOT, show_default=True,
              help="which writer's window to read")
@click.option("--all", "every", is_flag=True,
              help="read every window of the log pool")
@click.option("--base", type=_INT, default=None,
              help="read a raw address instead of a named slot")
@click.option("--out", type=click.Path(dir_okay=False, path_type=Path),
              help="also save the raw 64 KB buffer here")
@click.pass_context
def log(ctx: click.Context, slot: str, every: bool, base: int | None,
        out: Path | None) -> None:
    """Read back a log from the previous run.

    The windows are the shared log pool, see baremetal/README.md.
    """
    if every and base is not None:
        raise click.UsageError("do not combine --all with --base")

    windows = log_mod.windows_for(slot, base, every)
    if out and len(windows) > 1:
        raise click.UsageError(
            f"--out writes one buffer, but this reads {len(windows)}. "
            f"Pick a single window with --base"
        )

    ctx.exit(log_mod.run(windows, out=out))


def main() -> None:
    try:
        cli()
    except (NandError, ImageError) as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
