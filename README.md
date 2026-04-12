# AIPC OS

Reverse engineering and bare-metal development for the **AIPC netbook**, a
retro WinCE-based handheld netbook built on the **Anyka AK7802** SoC (ARM926EJ-S).
The long-term goal is a working Linux port.

See [https://aipc-os.catme0w.org/](https://aipc-os.catme0w.org/) for the
project homepage.

## Content

### Documentation (`docs/`)

Three layers of the boot chain, reverse-engineered from scratch:

- **[bootrom](docs/bootrom/README.md)** -- The mask ROM baked into the
  AK7802 die. USB boot mode, NAND/SPI boot, UART console, GPIO naming
  crosswalk, full memory map.
- **[nboot](docs/nboot/README.md)** -- First-stage NAND bootloader. DDR
  init script, self-relocation to upper DDR, EBOOT loading.
- **[EBOOT](docs/eboot/README.md)** -- WinCE second-stage bootloader.
  LCD bring-up (800x480, register-level cookbook), ENC28J60 SPI Ethernet,
  CH374 USB HID keyboard, NAND driver (4x528-byte interleaved ECC layout),
  vendor partition table (PTB), TFTP/EDBG download protocol, maintenance
  mode password and menu, GPIO driver with two independent pin numbering
  systems, CPU PLL formula.

### Bare-metal DOOM (`doom/`)

A [doomgeneric](https://github.com/ozkl/doomgeneric)-based DOOM port that
runs directly on AIPC. Loaded over USB boot mode.

### Boot methods (`boot/`)

- `coldboot/` -- Boot Linux directly from internal disk, bypassing WinCE entirely (WIP).
- `warmboot/` -- [HaRET](boot/warmboot/third_party/)-based Linux boot from within WinCE.

### Linux kernel (`kernel/`)

Kernel sources and patches (work in progress).

### Tools (`tools/`)

Python CLI tools (uv workspace) for talking to the device:

| Tool | Purpose |
| ---- | ------- |
| `ak7802-coldboot-dump` | Cold-boot attack RAM extraction |
| `ak7802-ddr-init` | Standalone DDR SDRAM init via USB boot |
| `ak7802-nand-dump` | Stream-mode NAND dump over USB (WIP) |
| `ak7802-nand-dump-min` | Host-driven NAND dump, one page per round-trip |
| `aipc-nand-extract` | Extract partitions from a raw NAND dump using PTB |
| `ak7802-usbboot` | USB boot mode protocol: peek, poke, upload, execute |

## Quick start

```
uv sync
```

This installs all Python tools into a shared virtualenv. CLI entry points
are available immediately:

```
uv run ak7802-usbboot --help
```

To build ARM stubs or the DOOM binary, you need `arm-none-eabi-gcc`.

## Hardware

- **SoC**: Anyka AK7802 (ARM926EJ-S, 248/266 MHz typical)
- **RAM**: 64 MB DDR SDRAM
- **Storage**: 512 MB MLC NAND (Hynix likely), 4x528-byte interleaved ECC layout
- **Display**: 800x480 TFT LCD, RGB565, ~48 Hz
- **Ethernet**: Microchip ENC28J60 on SPI2
- **USB HID**: WCH CH374 USB host bridge on SPI0 (internal keyboard)
- **USB**: MUSB (Mentor Graphics) integrated in SoC, 2 external USB-A ports

## License

See [LICENSE](LICENSE) for details. In short: tools and scripts are MIT,
kernel patches are GPLv2, docs are CC-BY-SA 4.0, DOOM is GPLv2.
