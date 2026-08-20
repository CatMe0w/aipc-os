# ARCHIVED

Do not run this. It reads a raw NAND dump without ECC correction and without unpacking the 4x528 interleaved page layout, so every partition it writes keeps the spare bytes inline and keeps any bit error the NAND returned. Output from this tool is wrong even when it looks plausible.

The successor is `tools/aipc-nand-extract`. This copy stays only to explain artifacts that were extracted before the ECC work landed. The rest of this file is the original text, unchanged.

Note: The Python package still declares the name `aipc-nand-extract`, the same name as its successor. This directory sits outside `tools/` so that the uv workspace never matches it.

# aipc-nand-extract

Extract partitions from an AIPC WinCE NAND dump.

It first finds the vendor `PTB` block near the end of NAND, then scans for the `NBT` entry as the start of the fixed `0x30`-byte partition records, and parses forward until `END`.

## Usage

```sh
uv run aipc-nand-extract nand.img -o out/
```

Without `-o`, files are written to an `extracted/` directory next to the input image.

## Output

The tool writes:

- `ptb.json`: parsed PTB metadata and derived extraction results
- `ptb.raw`: raw 4 KB PTB block
- `<tag>.raw`: PTB-selected full partition slices for every non-`END` PTB entry
- `nboot.nb0`, `eboot.nb0`, `eboot_bak.nb0`: payloads derived from known boot partitions
- `nboot_ddr_init.txt`: DDR/init register script extracted from the `ANYKA382` nboot wrapper
- `nk_ecec_XX.raw`: page-aligned `ECEC` sub-images found inside `nk.raw`

`nk.raw` is not expected to begin with `B000FF`. On this platform, `EBOOT` boots through the vendor `PTB` and then loads one or more `ECEC` images from the `NK` partition.
