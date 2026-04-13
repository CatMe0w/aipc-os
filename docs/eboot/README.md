# EBOOT

This directory contains reverse-engineered documentation for the AIPC EBOOT,
the WinCE second-stage bootloader that runs after nboot and before NK.bin.

## Scope

The analysis targets **firmware version 1.88**. An earlier v1.58.2 EBOOT also
exists on some units; v1.88 is likely a superset of v1.58.2 and is the primary subject
of this documentation. Version-specific notes call out the two where they
diverge.

EBOOT identifies itself at startup as:

```
Microsoft Windows CE Ethernet Bootloader Common Library Version 1.1 Built Oct 21 2009
```

This is a standard Windows CE 5.x OAL Ethernet Bootloader Common Library image,
customized by the OEM (Anyka reference-design derivative) for the AK7802 SoC.

## Position in the Boot Chain

```
Mask ROM bootrom -> nboot -> EBOOT -> NK (WinCE kernel)
(on-chip)           (NAND)   (NAND)   (NAND / TFTP)
```

- **bootrom** loads nboot from NAND block 0 into L2 SRAM and jumps.
  See [docs/bootrom/](../bootrom/README.md).
- **nboot** initializes DDR SDRAM, loads EBOOT from NAND into DDR, and jumps.
  See [docs/nboot/](../nboot/boot-flow.md).
- **EBOOT** is the subject of this directory. It performs full platform
  initialization, provides a keyboard-driven maintenance menu, runs a
  PTB-driven boot/config menu, and either boots the flash-resident NK image
  or enters the KITL / TFTP download path through the Ethernet backend
  selected by the PTB / BOOTARGS transport field (`ENC28J60` when nonzero,
  Bulverde RNDIS when zero).
- **NK** is the WinCE kernel proper; not documented here.

## Address and Handoff

EBOOT is stored in the `IPL` partition on NAND (block 2 on v1.88 test units).
nboot reads the first `0x64000` (400 KB) of that partition into DDR starting at
`0x30037FD4`, which places the `0x2C`-byte `IMG` wrapper header at
`0x30037FD4..0x30037FFF` and the first payload instruction at `0x30038000`.
nboot then branches to `0x30038000` in SVC mode.

EBOOT is linked for virtual `0x80038000` while executing from physical
`0x30038000`; later code reaches peripherals and DDR aliases through
`OALPAtoVA` and the OEMAddressTable baked into the image.

## Document Index

- [Memory Map and Register Reference](memory-map.md): DDR runtime layout,
  SYSCTRL registers eboot uses beyond the bootrom set, and new peripheral
  base addresses and usage (LCD, SPI).
- [Boot Flow](boot-flow.md): Top-level init sequence, main menu, and the
  handoff to NK.
- [Partition Format](partition-format.md): `PTB` block layout, entry table,
  the eight standard partition tags, factory defaults, and the `ECEC`
  sub-image container inside the `NK` partition.
- [GPIO Driver](gpio-driver.md): GPIO register model, two independent pin
  numbering systems, the 57-entry alt-function dispatch table, and the
  hypothesized GPIO interrupt controller.
- [NAND Driver](nand-driver.md): NAND sequencer usage, the `(512 data + 16
  ECC)` interleaved physical page layout, chip-database driven geometry, and
  the fresh-READ-per-chunk access pattern.
- [LCD Driver](lcd-driver.md): LCD controller register map, end-to-end
  bring-up sequence, 800x480 panel timing, framebuffer placement, and PWM
  backlight.
- [Ethernet Driver](ethernet-driver.md): ENC28J60 driver layer, the OEM
  Ethernet HAL vtable, BOOTME/TFTP/EDBG download state machine, and the
  hardcoded network defaults.
- [USB HID Input](usb-hid-input.md): CH374 USB-over-SPI bridge, HID
  boot-protocol keyboard path, and the maintenance-mode password gate.
- [Maintenance Mode](maintenance-mode.md): The hidden factory/service menu -
  menu items, format and update handlers, partition type mapping, and the
  "Format Nand disk" stub.

## Conventions

- Addresses are **physical** unless explicitly noted as virtual. EBOOT runs
  with a WinCE OEMAddressTable that maps all peripherals to two virtual
  regions (`0x8xxx_xxxx` cached, `0xAxxx_xxxx` uncached), but register tables
  list the physical addresses for cross-referencing with the bootrom docs.
- Function names used in this documentation (`enc28j60_init`,
  `ptb_build_default_in_ram`, etc.) are the names applied to the IDA database
  for `eboot.clean.nb0`; they are not symbols present in the
  original binary.
- Items marked `[unverified]`, `[hypothesis]`, or `[partial]` are inline
  warnings. Each document also has an `Unresolved` section at the end that
  aggregates all open questions for that topic.

## Verifying the Analyzed EBOOT

The current IDA database is
`confidential/gray_extracted/eboot.clean.nb0.i64`, whose module payload is
`confidential/gray_extracted/eboot.clean.nb0`. Its size is `0x64000` bytes
(400 KB), matching the fixed load size nboot uses. This is the wrapper-stripped
EBOOT payload: byte 0 is the first ARM instruction at virtual `0x80038000`,
not the preceding `IMG` header that nboot leaves at `0x30037FD4..0x30037FFF`.
