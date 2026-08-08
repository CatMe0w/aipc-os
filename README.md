# AIPC OS

Reverse engineering and bare-metal development for the **AIPC netbook**, a retro WinCE-based handheld netbook built on the **Anyka AK7802** SoC (ARM926EJ-S). The long-term goal is a working Linux _(and DOOM)_ port.

See [https://aipc-os.catme0w.org/](https://aipc-os.catme0w.org/) for the project homepage.

## Showcase

| Booting Linux 7.0-rc3 | Booting DOOM |
| --- | --- |
| ![Booting Linux 7.0-rc3](https://github.com/user-attachments/assets/9385af0d-8bfa-4284-a66e-5d4783e4fc9a) | ![Booting DOOM](https://github.com/user-attachments/assets/d2f49c3f-eb05-4561-8703-38333c41fbfc) |

## Content

### Documentation ([`docs/`](docs/))

- **[bootrom](docs/bootrom/README.md)** -- The mask ROM baked into the AK7802 die. USB boot mode, NAND/SPI boot, UART console, GPIO naming crosswalk, full memory map.
- **[nboot](docs/nboot/README.md)** -- First-stage NAND bootloader. DDR init script, EBOOT loading.
- **[EBOOT](docs/eboot/README.md)** -- WinCE second-stage bootloader. LCD bring-up, ENC28J60 SPI Ethernet, CH374 USB HID keyboard, NAND driver, vendor partition table, TFTP/EDBG download protocol, maintenance mode password and menu, GPIO driver with two independent pin numbering systems, CPU PLL formula.
- **[NK](docs/nk/README.md)** -- WinCE kernel and vendor drivers.

Reverse-engineered from scratch.

### Bare-metal DOOM ([`doom/`](doom/))

A [doomgeneric](https://github.com/ozkl/doomgeneric)-based DOOM port that runs directly on AIPC.

### Boot methods ([`boot/`](boot/))

- `coldboot/` -- Boot Linux directly from internal disk, bypassing WinCE entirely.
- `warmboot/` -- [HaRET](boot/warmboot/third_party/)-based Linux boot from within WinCE.

### Linux kernel ([`kernel/`](kernel/))

Kernel sources and patches.

### Tools ([`tools/`](tools/))

Python CLI tools (uv workspace) for talking to the device:

| AK7802 SoC Tool        | Purpose                                             |
| ---------------------- | --------------------------------------------------- |
| `ak7802-nand-dump-min` | Universal AK7802 NAND dump tool                     |
| `ak7802-usbboot`       | USB boot mode protocol: peek, poke, upload, execute |

| AIPC-specific Tool   | Purpose                                 |
| -------------------- | --------------------------------------- |
| `aipc-coldboot-dump` | Cold-boot attack RAM extraction         |
| `aipc-ddr-init`      | Standalone DDR SDRAM init via USB boot  |
| `aipc-nand-dump`     | Fast NAND dump tool for AIPC            |
| `aipc-nand-extract`  | Extract partitions from a raw NAND dump |

| Extra     | Purpose                                                                               |
| --------- | ------------------------------------------------------------------------------------- |
| `gdbstub` | GDB stub, a replacement for the bootrom USB boot mode                                 |
| `probes`  | Lab reports and ARM assembly probes for investigating hardware behavior (e.g. SD/MMC) |
| `old`     | Deprecated tools and scripts from early experimentation                               |

### Website ([`website/`](website/))

Source for [aipc-os.catme0w.org](https://aipc-os.catme0w.org/).

## Quick start

```
uv sync
```

This installs all Python tools into a shared virtualenv. CLI entry points are available immediately:

```
uv run ak7802-usbboot --help
```

To build ARM stubs or the DOOM binary, you need `arm-none-eabi-gcc`.

## Hardware

- **SoC**: Anyka AK7802 (ARM926EJ-S, 248/266 MHz typical)
- **RAM**: 64 MB DDR SDRAM
- **Storage**: 512 MB MLC NAND (Hynix typical), 4x528-byte interleaved ECC layout
- **Display**: 800x480 TFT LCD, RGB565, ~48 Hz
- **Ethernet**: Davicom DM9000A, 8-bit parallel bus bit-banged over GPIO
- **USB HID**: WCH CH374 USB host bridge on SPI, internal keyboard + 2 external USB-A ports
- **USB**: MUSB (Mentor Graphics) integrated in SoC, 1 external USB-A port

## License

See [LICENSE](LICENSE) for details. In short: tools and scripts are MIT, kernel patches are GPLv2, docs are CC-BY-SA 4.0, DOOM is GPLv2.
