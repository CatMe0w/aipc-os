# AIPC DOOM

Bare-metal DOOM port for the AIPC netbook, based on [doomgeneric](https://github.com/ozkl/doomgeneric).

## Quick start

Requires `arm-none-eabi-gcc` with newlib.

Put the device in USB boot mode (`DL_JUMP` or `USB_BOOT` connected at power-on), then:

```sh
git submodule update --init doomgeneric # acquire doomgeneric sources, only needed for the first time
make
./boot.sh
```

> **macOS:** The Homebrew `arm-none-eabi-gcc` does not include newlib. Install [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) instead; the Makefile auto-detects it.

## Playing with other DOOM versions (optional)

Purchase [DOOM + DOOM II](https://store.steampowered.com/app/2280/DOOM__DOOM_II/) on Steam. It includes all four supported WADs.

**Finding the install directory:** right-click the game in Steam -> _Manage_ -> _Browse local files_

| Path in install directory    | Copy to `wad/` | `WAD=`     |
| ---------------------------- | -------------- | ---------- |
| `base/DOOM.WAD`              | `DOOM.WAD`     | `doom1`    |
| `base/doom2/DOOM2.WAD`       | `DOOM2.WAD`    | `doom2`    |
| `base/tnt/TNT.WAD`           | `TNT.WAD`      | `tnt`      |
| `base/plutonia/PLUTONIA.WAD` | `PLUTONIA.WAD` | `plutonia` |

Copy the WAD file into `wad/`. Then build and boot with the matching version name:

```sh
make WAD=doom2
./boot.sh doom2
```

## Firmware versions

In most cases the firmware version does not need to be specified. If DDR initialization fails, pass it as the second argument (`1.88` by default, or `1.58.2` for older boards. See `tools/aipc-ddr-init/README.md`):

```sh
./boot.sh doom2 1.58.2
```

## Memory layout

| Address      | Size   | Used by                                 |
| ------------ | ------ | --------------------------------------- |
| `0x301A0000` | 64 KB  | Log window                              |
| `0x30200000` | ~8 MB  | doom\*.bin (code + data + heap + stack) |
| `0x30B00000` | varies | IWAD (shareware ~4 MB, full ~14 MB)     |
| `0x33B00000` | 750 KB | Framebuffer (800x480 RGB565)            |

Read the log back after a hang with `uv run opennboot log --slot doom`.
