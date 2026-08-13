# bootrom

This directory holds reverse-engineered documentation for the AK7802 bootrom.

## Index

- [AK7802 Bootrom Overview](overview.md): bootrom layout, exception vectors, and runtime tasks.
- [Boot Flow](boot-flow.md): reset-time initialization and boot source selection.
- [Memory Map and Register Reference](memory-map.md): memory regions, peripherals, and registers that the bootrom touches.
- [Boot Image Format](boot-image-format.md): the common image header, its layout, and the validation rules of the storage boot paths.
- [SPI Flash Boot Path](spi-boot.md): SPI NOR probe and image load.
- [NAND Flash Boot Path](nand-boot.md): NAND controller setup, probe, and image load.
- [USB Boot Mode](usb-boot.md): USB boot and download mode behavior, and the host protocol.
- [AP2-BIOS UART Console](uart-console.md): UART console entry conditions and command interface.
- [Diagnostic Self-Test Mode](diag-mode.md): factory diagnostic boot mode and self-test behavior.
- [GPIO Naming Crosswalk](gpio-naming-crosswalk.md): AIPC OS GPIO naming against schematic `GPIOn` / `DGPIOn` naming, and the board nets.

The function names in these documents are our names from the reverse engineering. [overview.md](overview.md) gives the address of each one.
