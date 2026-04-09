# aipc-nand-extract

Extract partitions from an AIPC WinCE NAND dump.

It first finds the vendor `PTB` block near the end of NAND, then scans for the
`NBT` entry as the start of the fixed `0x30`-byte partition records, and parses
forward until `END`.

## Usage

```sh
uv run aipc-nand-extract nand.img -o out/
```

Without `-o`, files are written to an `extracted/` directory next to the
input image.

## Output

The tool writes:

- `ptb.json`: parsed PTB metadata and derived extraction results
- `ptb.raw`: raw 4 KB PTB block
- `<tag>.raw`: PTB-selected full partition slices for every non-`END` PTB entry
- `nboot.nb0`, `eboot.nb0`, `eboot_bak.nb0`: payloads derived from known boot partitions
- `nboot_ddr_init.txt`: DDR/init register script extracted from the `ANYKA382` nboot wrapper
- `nk_ecec_XX.raw`: page-aligned `ECEC` sub-images found inside `nk.raw`

`nk.raw` is not expected to begin with `B000FF`. On this platform, `EBOOT`
boots through the vendor `PTB` and then loads one or more `ECEC` images from
the `NK` partition.
