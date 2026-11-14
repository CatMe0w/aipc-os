from __future__ import annotations

import sys
from pathlib import Path

import click

from . import backup_nand as backup_nand_mod
from . import install as install_mod
from . import log as log_mod
from . import restore as restore_mod
from . import restore_nand as restore_nand_mod
from . import run as run_mod
from .images import ImageError
from .nand import NandError


class _HexInt(click.ParamType):
    name = "integer"

    def convert(self, value, param, ctx):
        if isinstance(value, int):
            return value
        try:
            return int(value, 0)
        except ValueError:
            self.fail(f"{value!r} is not a valid integer (decimal or 0x-prefixed hexadecimal)", param, ctx)


_INT = _HexInt()

_row = click.option("--row", type=int, default=0, hidden=True)


@click.group()
@click.version_option(package_name="opennboot")
def cli() -> None:
    """Install and maintain openNBOOT, a replacement bootloader for the AIPC netbook.

    All commands require the device to be in USB boot mode.
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
              help="Backup file created by 'install' (backup_nboot-*.bin).")
@_row
@click.pass_context
def restore(ctx: click.Context, image: Path, row: int) -> None:
    """Restore the original bootloader to NAND block 0."""
    ctx.exit(restore_mod.run(image, row=row))


@cli.command("backup-nand")
@click.option("--pages", type=click.IntRange(1), default=None, hidden=True)
@click.pass_context
def backup_nand(ctx: click.Context, pages: int | None) -> None:
    """Back up the entire NAND to an image file in the current directory."""
    ctx.exit(backup_nand_mod.run(pages=pages))


@cli.command("restore-nand")
@click.argument("image", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.option("--first-block", type=click.IntRange(0), default=0, hidden=True)
@click.option("--blocks", type=click.IntRange(1), default=None, hidden=True)
@click.pass_context
def restore_nand(ctx: click.Context, image: Path, first_block: int,
                 blocks: int | None) -> None:
    """Erase the NAND and write an image file to it."""
    ctx.exit(restore_nand_mod.run(image, first_block=first_block, blocks=blocks))


@cli.command()
@click.option("--addr", type=_INT, default=run_mod.LOAD_ADDR, hidden=True)
@click.pass_context
def run(ctx: click.Context, addr: int) -> None:
    """Run openNBOOT from memory without modifying the NAND."""
    ctx.exit(run_mod.run(addr=addr))


@cli.command()
@click.option("--slot", type=click.Choice(list(log_mod.SLOTS)),
              default=log_mod.DEFAULT_SLOT, show_default=True,
              help="Log window to read.")
@click.option("--all", "every", is_flag=True,
              help="Read all windows of the log pool.")
@click.option("--base", type=_INT, default=None,
              help="Read from a raw address instead of a named slot.")
@click.option("--out", type=click.Path(dir_okay=False, path_type=Path),
              help="Also write the raw 64 KB buffer to this file.")
@click.pass_context
def log(ctx: click.Context, slot: str, every: bool, base: int | None,
        out: Path | None) -> None:
    """Read the log of the previous run from device memory.

    The windows belong to the shared log pool described in baremetal/README.md.
    """
    if every and base is not None:
        raise click.UsageError("--all and --base are mutually exclusive")

    windows = log_mod.windows_for(slot, base, every)
    if out and len(windows) > 1:
        raise click.UsageError(
            f"--out accepts a single buffer, but {len(windows)} windows were "
            f"selected. Select one window with --base"
        )

    ctx.exit(log_mod.run(windows, out=out))


def main() -> None:
    try:
        cli()
    except (NandError, ImageError) as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
