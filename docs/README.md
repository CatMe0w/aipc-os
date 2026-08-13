# Documentation

This directory holds reverse-engineering notes and reference documents for the Anyka AK7802 SoC and the AIPC netbook.

## Index

- [bootrom](bootrom/README.md): AK7802 SoC bootrom architecture, boot flow, boot sources, interfaces, and image format.
- [nboot](nboot/README.md): first-stage bootloader of AIPC netbook firmware v1.58.2 and v1.88, its architecture and boot flow.
- [eboot](eboot/README.md): second-stage bootloader WinCE EBOOT. Memory map, GPIO, NAND, LCD and Ethernet drivers, partition format, download protocol, and maintenance mode.
- [nk](nk/README.md): WinCE NK image internals. NK child partition layout, ECEC scatter-load container, ROMHDR and ROM module and file tables, and ROM module PE rebuild.

## License

CC-BY-SA 4.0. See `LICENSE` for details.
