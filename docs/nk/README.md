# NK

This directory documents the WinCE NK image in the `NK` partition of the AIPC NAND. It covers the child partition layout inside `NK`, the Anyka ECEC scatter-load container, the WinCE ROM metadata tables, and the way to rebuild a ROM module into a PE file that a decompiler can use.

## Position in the Boot Chain

```
Mask ROM bootrom -> nboot -> EBOOT -> NK (WinCE kernel)
(on-chip)           (NAND)   (NAND)   (NAND)
```

- **bootrom** loads nboot from block 0 into L2 SRAM. See [docs/bootrom/](../bootrom/README.md).
- **nboot** initializes DDR and loads EBOOT. See [docs/nboot/](../nboot/README.md).
- **EBOOT** reads the PTB, validates an `ECEC` magic at the start of the BINFS sub-partition, and gives control to the WinCE kernel. See [docs/eboot/](../eboot/README.md) for the PTB format and the ECEC verification in EBOOT.
- **NK** is the subject of this directory.

## Document Index

- [Partition and ECEC Layout](partition-and-ecec-layout.md): NK child partition table, ECEC image header, periodic metadata pages, logical-to-raw-offset formula, and chain information record.
- [ROMHDR and TOC](romhdr-and-toc.md): WinCE `ROMHDR` structure, ROM module table, ROM file table, compact `e32_rom` header, and compact `o32_rom` section descriptors.
- [Module Rebuild](module-rebuild.md): how to turn a ROM module descriptor into a PE file for a decompiler, with image base selection, section byte extraction, in-image pointer relocation, export directory synthesis, and import directory exposure.
- [Display Driver](display-driver.md): LCD MMIO mapping, framebuffer address model, and confirmed LCD register fields.
- [SDHC Driver](sdhc-driver.md): command, PIO, L2 DMA, clock and interrupt behavior of the WinCE `sdhc_anyka.dll`.
- [DM9000 Ethernet Driver](dm9000-driver.md): DM9000A board wiring, the GPIO-timed parallel bus protocol, and the structure of the WinCE `dm9000x.dll`.
- [Power Management](power-management.md): WinCE power-state policy, OAL poweroff through GPIO105, and the unverified RTC reboot path.

## Conventions

- An offset marked **blob offset** is a byte position in the relevant `NK.ecec_NN.raw` file, after the metadata pages go back in their stored positions.
- An offset marked **logical offset** is `pointer - load_base`. It treats the ECEC image as a flat virtual address space with no metadata pages.
- A WinCE address (`physfirst`, `load_pointer`, `name_pointer`, and so on) is always virtual unless the text says otherwise.
- Field names (`physfirst`, `nummods`, `e32_pointer`, and so on) are the WinCE Platform Builder names where those are known. Names for ECEC-specific fields (`field_44`, `field_48`) are analysis names, because no symbol information exists.
- Items marked `[unverified]` or `[partial]` are inline warnings. A document can also have an `Unresolved` section.

## Verifying the Analyzed Image

The primary analysis target is `NK.ecec_01.raw` from the v1.88 unit. The module rebuild output is under `NK.ecec_01.modules/`. The v1.58.2 unit gives a cross-check for the structure layout and the observed field values.
