# aipc-nand-extract

Normalize and extract AIPC 2112-byte/page raw NAND dumps.

## Usage

Raw nand dumps produced by `tools/nand-dump` are required.

```
uv run aipc-nand-extract dump.bin
```

With an explicit output directory:

```
uv run aipc-nand-extract dump.bin nand_extracted
```

## Flow

The tool does one fixed extraction flow. It has no mode flags.

1. Scan the raw dump for valid `PTB` snapshots using temporary interleaved-page
   normalization.
2. Parse the newest valid PTB snapshot.
3. Derive NAND block geometry from the parsed `END` entry.
4. Normalize the full raw dump to `nand.clean.bin`.
5. Split the clean dump into PTB partitions named `<TAG>.raw`.
6. Export known analysis-ready views for bootloader and NK tooling.
7. Write NAND geometry, parsed PTB data, and view provenance to
   `nand_extract.json`.

PTB entry records are found by the strict tag sequence
`NBT, IPL, BAK, UDR, NK, DSK, CFG, END`, not by a fixed table offset. This
covers the observed 1.88 layout at `PTB+0x670` and the 1.58.2 layout at
`PTB+0x230`.

Most pages are normalized as interleaved pages. `NBT` is special: pages whose
final 64 raw bytes are all `0xFF` are treated as plain `2048B data + 64B (unused) OOB`
pages, while the rest are normalized as interleaved pages.
