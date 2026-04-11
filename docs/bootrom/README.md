# bootrom

This directory contains reverse-engineered documentation for the AK7802 bootrom.

## Index

- [AK7802 Bootrom Overview](overview.md): High-level description of the bootrom layout, exception vectors, and runtime responsibilities.
- [Boot Flow](boot-flow.md): Reset-time initialization and boot source selection flow.
- [Memory Map and Register Reference](memory-map.md): Memory regions, peripherals, and registers touched by the bootrom.
- [Boot Image Format](boot-image-format.md): Common image header, layout, and validation rules used by storage boot paths.
- [SPI Flash Boot Path](spi-boot.md): SPI NOR probe and image loading procedure.
- [NAND Flash Boot Path](nand-boot.md): NAND controller setup, probing, and image loading procedure.
- [USB Boot Mode](usb-boot.md): USB boot/download mode behavior and host protocol.
- [AP2-BIOS UART Console](uart-console.md): UART console entry conditions and command interface.
- [Diagnostic Self-Test Mode](diag-mode.md): Factory diagnostic boot mode and self-test behavior.
- [GPIO Naming Crosswalk](gpio-naming-crosswalk.md): AIPC OS GPIO naming vs schematic `GPIOn` / `DGPIOn` naming and board net mapping.
