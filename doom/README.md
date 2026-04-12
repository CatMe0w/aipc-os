# AIPC DOOM

Bare-metal DOOM port for the AIPC netbook, based on [doomgeneric](https://github.com/ozkl/doomgeneric).

## Building

Requires `arm-none-eabi-gcc` with newlib.

```
make
```

> **macOS:** The Homebrew `arm-none-eabi-gcc` package does not include newlib. Use the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) instead and set `CROSS` accordingly:
> ```
> make CROSS=/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/arm-none-eabi-
> ```

## Running

The device must be in USB boot mode (DGPIO[2] high at power-on). `ak7802-usbboot` must be available (see `tools/usbboot`).

```
cd doom
./boot.sh
```

`boot.sh` performs four steps:

1. DDR initialization via `poke` commands (mirrors the nboot header script)
2. Upload `doom.bin` to `0x30000000`
3. Upload `DOOM1.WAD` to `0x30900000`
4. Execute at `0x30000000`

For v1.88 hardware, see the version note in `boot.sh` before running.

## Memory layout

| Address      | Size   | Usage                                 |
| ------------ | ------ | ------------------------------------- |
| `0x30000000` | ~8 MB  | doom.bin (code + data + heap + stack) |
| `0x30900000` | ~4 MB  | DOOM1.WAD                             |
| `0x33ed3c00` | 750 KB | Framebuffer (800x480 RGB565)          |
