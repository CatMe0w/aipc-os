# EBOOT

This directory holds reverse-engineered documentation for the AIPC EBOOT, the WinCE second-stage bootloader that runs after nboot and before NK.bin.

## Scope

The analysis targets **firmware version 1.88**. An earlier v1.58.2 EBOOT also exists on some units. v1.88 is probably a superset of v1.58.2, and it is the primary subject here. Version-specific notes mark the places where the two differ.

EBOOT identifies itself at startup as:

```
Microsoft Windows CE Ethernet Bootloader Common Library Version 1.1 Built Oct 21 2009
```

This is a standard Windows CE 5.x OAL Ethernet Bootloader Common Library image. The OEM, an Anyka reference-design derivative, customized it for the AK7802 SoC.

## Position in the Boot Chain

```
Mask ROM bootrom -> nboot -> EBOOT -> NK (WinCE kernel)
(on-chip)           (NAND)   (NAND)   (NAND / TFTP)
```

- **bootrom** loads nboot from NAND block 0 into L2 SRAM and jumps. See [docs/bootrom/](../bootrom/README.md).
- **nboot** initializes DDR SDRAM, loads EBOOT from NAND into DDR, and jumps. See [docs/nboot/](../nboot/README.md).
- **EBOOT** is the subject of this directory. It initializes the whole platform, offers a keyboard-driven maintenance menu, runs a PTB-driven boot and config menu, and then either boots the flash-resident NK image or enters the KITL and TFTP download path. The transport field in PTB and BOOTARGS selects the Ethernet backend: `ENC28J60` for a non-zero value, Bulverde RNDIS for zero.
- **NK** is the WinCE kernel proper. See [docs/nk/](../nk/README.md).

## Address and Handoff

EBOOT lives in the `IPL` partition on NAND, block 2 on the v1.88 test units. nboot reads the first `0x64000` bytes, 400 KB, of that partition into DDR from `0x30037FD4`. That places the `0x2C`-byte `IMG` wrapper header at `0x30037FD4..0x30037FFF` and the first payload instruction at `0x30038000`. nboot then branches to `0x30038000` in SVC mode.

EBOOT is linked for virtual `0x80038000` but executes from physical `0x30038000`. Later code reaches the peripherals and the DDR aliases through `OALPAtoVA` and the OEMAddressTable in the image.

## Document Index

- [Memory Map and Register Reference](memory-map.md): DDR runtime layout, the SYSCTRL registers that eboot uses beyond the bootrom set, and new peripheral base addresses and their use (LCD, SPI).
- [Boot Flow](boot-flow.md): top-level init sequence, main menu, and the handoff to NK.
- [Partition Format](partition-format.md): `PTB` block layout, entry table, the eight standard partition tags, factory defaults, and the `ECEC` sub-image container inside the `NK` partition.
- [GPIO Driver](gpio-driver.md): GPIO register model, two independent pin numbering systems, the 57-entry alt-function dispatch table, and the hypothesized GPIO interrupt controller.
- [NAND Driver](nand-driver.md): NAND sequencer usage, the interleaved physical page layout of 512 data bytes plus 16 ECC bytes, chip-database driven geometry, and the fresh-READ-per-chunk access pattern.
- [LCD Driver](lcd-driver.md): LCD controller register map, end-to-end bring-up sequence, 800x480 panel timing, framebuffer placement, and PWM backlight.
- [Ethernet Driver](ethernet-driver.md): ENC28J60 driver layer, the OEM Ethernet HAL vtable, the BOOTME/TFTP/EDBG download state machine, and the hardcoded network defaults.
- [USB HID Input](usb-hid-input.md): CH374 USB-over-SPI bridge, HID boot-protocol keyboard path, and the maintenance-mode password gate.
- [Maintenance Mode](maintenance-mode.md): the hidden factory and service menu - menu items, format and update handlers, partition type mapping, and the "Format Nand disk" stub.

## Conventions

- An address is **physical** unless the text says virtual. EBOOT runs with a WinCE OEMAddressTable that maps all peripherals into two virtual regions, `0x8xxx_xxxx` cached and `0xAxxx_xxxx` uncached, but the register tables list the physical addresses for a cross-reference with the bootrom documents.
- Most function names here (`enc28j60_init`, `ptb_build_default_in_ram`, and so on) are ours, from the reverse engineering of `IPL.eboot.bin`. A few, such as `OEMEthSendFrame`, come from a contract outside the image. The Function Addresses section of [memory-map.md](memory-map.md) gives the address of every named function, and says which names are not ours.
- Items marked `[unverified]`, `[hypothesis]` or `[partial]` are inline warnings. Each document also ends with an `Unresolved` section that collects the open questions for that topic.
