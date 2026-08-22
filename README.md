# AIPC OS

Reverse engineering and bare-metal development for the **AIPC netbook** (a.k.a. Zenithink ZT-N670 or Disgo 3000), a retro WinCE-based handheld netbook built on the **Anyka AK7802** SoC (ARM926EJ-S). The long-term goal is a working Linux _(and DOOM)_ port.

See [https://aipc-os.catme0w.org/](https://aipc-os.catme0w.org/) for the project homepage.

## Showcase

| Booting Linux 7.0-rc3 | Booting DOOM | Booting [AOSC OS Afterglow](https://aosc.io/afterglow)
| --- | --- | --- |
| ![Booting Linux 7.0-rc3](https://github.com/user-attachments/assets/9385af0d-8bfa-4284-a66e-5d4783e4fc9a) | ![Booting DOOM](https://github.com/user-attachments/assets/d2f49c3f-eb05-4561-8703-38333c41fbfc) | ![Booting AOSC OS Afterglow](https://github.com/user-attachments/assets/81570855-eae7-431d-87ad-99dd01d00ed2) |

## Content

### Documentation ([`docs/`](docs/))

- **[bootrom](docs/bootrom/README.md)**: The mask ROM baked into the AK7802 die. USB boot mode, NAND/SPI boot, UART console, GPIO naming crosswalk, full memory map.
- **[nboot](docs/nboot/README.md)**: First-stage NAND bootloader. DDR init script, EBOOT loading.
- **[EBOOT](docs/eboot/README.md)**" WinCE second-stage bootloader. LCD bring-up, ENC28J60 SPI Ethernet, CH374 USB HID keyboard, NAND driver, vendor partition table, TFTP/EDBG download protocol, maintenance mode password and menu, GPIO driver with two independent pin numbering systems, CPU PLL formula.
- **[NK](docs/nk/README.md)**: WinCE kernel and vendor drivers.
- **[AIPC OS Original](docs/aipc-os-original/README.md)**: Original research from us. Johnson–Nyquist noise TRNG, faster SD/MMC driver.

Reverse-engineered from scratch.

### Bare metal ([`baremetal/`](baremetal/))

Code that runs on the AK7802 with no operating system.

- `opennboot/`: Custom firmware openNBOOT. Replaces the stock nboot in NAND block 0 and boots arbitrary ARM payloads from SD.
- `aipc-boot/`: The payload openNBOOT hands off to. An GUI menu that boots a Linux zImage or the GDB stub from SD, or stock WinCE from NAND.
- `gdbstub/`: GDB stub, a replacement for the bootrom USB boot mode.
- `doom/`: A [doomgeneric](https://github.com/ozkl/doomgeneric)-based DOOM port.
- `probes/`: One-shot ARM probes and lab reports. Not for reuse.
- `lib/`: Shared drivers. LCD, keyboard, MMU, SD, FAT, NAND, UART, etc.

### SD card ([`sdcard/`](sdcard/))

Recipe for the SD card image, and the HaRET files for the warm boot path.

### Linux kernel ([`kernel/`](kernel/))

Kernel sources and patches.

### Tools ([`tools/`](tools/))

Host-side Python CLI tools, one uv workspace member per directory. The `ak7802-` prefix means the tool works on any AK7802. The `aipc-` prefix means it depends on something specific to this device.

| Tool                   | Purpose                                             |
| ---------------------- | --------------------------------------------------- |
| `opennboot`            | Install openNBOOT into NAND                         |
| `ak7802-usbboot`       | USB boot mode protocol: peek, poke, upload, execute |
| `ak7802-nand-dump-min` | Universal AK7802 NAND dump tool                     |
| `aipc-coldboot-dump`   | Cold-boot attack RAM extraction                     |
| `aipc-ddr-init`        | Standalone DDR SDRAM init via USB boot              |
| `aipc-nand-dump`       | Fast NAND dump tool for AIPC                        |
| `aipc-nand-extract`    | Extract partitions from a raw NAND dump             |

### Talks ([`talks/`](talks/))

Slides from public talks about this project.

### Website ([`website/`](website/))

Source for [aipc-os.catme0w.org](https://aipc-os.catme0w.org/).

### Attic ([`attic/`](attic/))

Superseded code, kept only to explain artifacts it produced.

## Quick start

```
uv sync
```

CLI entry points are available immediately:

```
uv run ak7802-usbboot --help
```

To build ARM stubs or the DOOM binary, you need `arm-none-eabi-gcc`.

## Hardware

- **SoC**: Anyka AK7802 (ARM926EJ-S, 248 MHz typical)
- **RAM**: 64 MB DDR SDRAM
- **Storage**: 512 MB MLC NAND (Hynix typical), 4x528-byte interleaved ECC layout
- **Display**: 800x480 TFT LCD, RGB565
- **Ethernet**: Davicom DM9000A, 8-bit parallel bus bit-banged over GPIO
- **USB HID**: WCH CH374 USB host bridge on SPI, internal keyboard + 2 external USB-A ports
- **USB**: MUSB (Mentor Graphics) integrated in SoC, 1 external USB-A port

## License

See [LICENSE](LICENSE) for details. In short: tools and scripts are MIT, kernel patches are GPLv2, DOOM is GPLv2, HaRET is GPLv2, docs are CC-BY-SA 4.0, talks are CC-BY 4.0.
