# NK

This directory documents the WinCE NK image stored in the `NK` partition of the AIPC NAND. It covers the child partition layout inside `NK`, the Anyka ECEC scatter-load container, the WinCE ROM metadata tables, and the methodology for rebuilding ROM modules into decompiler-usable PE files.

## Position in the Boot Chain

```
Mask ROM bootrom -> nboot -> EBOOT -> NK (WinCE kernel)
(on-chip)           (NAND)   (NAND)   (NAND)
```

- **bootrom** loads nboot from block 0 into L2 SRAM. See [docs/bootrom/](../bootrom/README.md).
- **nboot** initializes DDR and loads EBOOT. See [docs/nboot/](../nboot/README.md).
- **EBOOT** reads the PTB, validates an `ECEC` magic at the start of the BINFS sub-partition, and hands off to the WinCE kernel. See [docs/eboot/](../eboot/README.md) for the PTB format and EBOOT's ECEC verification.
- **NK** is the subject of this directory.

## Document Index

- [Partition and ECEC Layout](partition-and-ecec-layout.md): NK child partition table, ECEC image header, periodic metadata pages, logical-to-raw-offset formula, and chain information record.
- [ROMHDR and TOC](romhdr-and-toc.md): WinCE `ROMHDR` structure, ROM module table, ROM file table, compact `e32_rom` header, and compact `o32_rom` section descriptors.
- [Module Rebuild](module-rebuild.md): Converting ROM module descriptors into decompiler-oriented PE files — image base selection, section byte extraction, in-image pointer relocation, export directory synthesis, and import directory exposure.
- [Display Driver](display-driver.md): LCD MMIO mapping, framebuffer address model, and confirmed LCD register fields.

## Conventions

- Offsets marked **blob offset** are byte positions within the relevant `NK.ecec_NN.raw` file, after metadata pages have been placed back in their stored positions.
- Offsets marked **logical offset** are `pointer - load_base`, which treats the ECEC image as a flat virtual address space with no metadata pages.
- WinCE virtual addresses (`physfirst`, `load_pointer`, `name_pointer`, etc.) are always virtual unless stated otherwise.
- Field names (`physfirst`, `nummods`, `e32_pointer`, etc.) are the WinCE Platform Builder names where known. Names for ECEC-specific fields (`field_44`, `field_48`) are analysis names because no symbol information is available.
- Items marked `[unverified]` or `[partial]` are inline warnings. Each document also has an `Unresolved` section.

## Verifying the Analyzed Image

The primary analysis target is `NK.ecec_01.raw` from the v1.88 unit. Module rebuild output is under `NK.ecec_01.modules/`. The v1.58.2 unit provides a cross-check for structure layout and observed field values.
