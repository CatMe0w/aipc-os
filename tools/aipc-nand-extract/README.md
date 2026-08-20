# aipc-nand-extract

Normalize and extract AIPC 2112-byte/page raw NAND dumps.

## Usage

Raw NAND dumps produced by `tools/aipc-nand-dump` are required.

```
uv run aipc-nand-extract dump.bin
```

With an explicit output directory:

```
uv run aipc-nand-extract dump.bin nand_extracted
```

## Flow

The tool does one fixed extraction flow. It has no mode flags.

1. Scan the raw dump for candidate `PTB` pages using temporary interleaved-page normalization.
2. Parse the newest valid PTB snapshot. The PTB entry table is located by matching the strict tag sequence `NBT, IPL, BAK, UDR, NK, DSK, CFG, END`, which covers both the v1.88 layout at `PTB+0x670` and the v1.58.2 layout at `PTB+0x230`.
3. Derive NAND block geometry from the `END` entry and raw dump size.
4. Normalize the full raw dump to `nand.clean.bin`. Most partitions use interleaved pages (`4 × (512 data + 16 OOB/ECC)` -> 2048 bytes). `NBT` is special: pages whose final 64 raw bytes are all `0xFF` are treated as plain `2048B data + 64B OOB` pages. The rest are normalized as interleaved.
5. Split the clean dump into PTB partitions named `<TAG>.raw`.
6. Export analysis-ready views: `NBT.code.bin` (nboot payload), `IPL.eboot.bin` / `BAK.eboot.bin` (EBOOT payload, wrapper-stripped).
7. Parse the `NK` partition's MBR child partition table and write `NK.binfs.raw` (type `0x21`) and `NK.fat.raw` (type `0x04`).
8. Detect ECEC images in `NK.binfs.raw` by scanning for the `ECEC` magic at page-aligned offsets. Determine the global metadata-page period and per-image metadata-page index. Resolve logical sizes from the embedded `chain information` record when present.
9. Write each ECEC image as `NK.ecec_NN.raw`.
10. Parse each ECEC image's `ROMHDR` and ROM module/file tables. Convert all virtual-address pointers to blob offsets via the metadata-page skip formula.
11. Rebuild ROM modules into decompiler-oriented PE files under `NK.ecec_NN.modules/`. `ImageBase` is set to `module.load_pointer`. Section bytes are read from each section's own `o32_rom.data_pointer`. CECOMPRESS sections are decompressed when their block headers validate. In-image absolute pointers are rebased from the compact descriptor's `image_base` to `load_pointer`. The export directory is synthesized into a new `.edata` section with adjusted RVAs from `e32_rom.units[0]`. The import directory from `e32_rom.units[1]` is exposed only when it validates as PE-like import descriptors.
12. Write NAND geometry, parsed PTB data, view provenance, ECEC metadata, ROMHDR tables, and rebuilt-module metadata to `nand_extract.json`.

## NAND Geometry

Two firmware variants have been observed, both producing a 512 MiB clean image from the same 528 MiB raw dump:

| Firmware | Raw page | Clean page | Block size | Pages/block | Blocks |
| -------- | -------: | ---------: | ---------: | ----------: | -----: |
| v1.58.2  |   2112 B |     2048 B |    128 KiB |          64 |   4096 |
| v1.88    |   2112 B |     2048 B |    256 KiB |         128 |   2048 |

See [docs/eboot/nand-driver.md](../../docs/eboot/nand-driver.md) for the physical page layout and OOB/ECC details.

## Output

| File | Contents |
| --- | --- |
| `nand.clean.bin` | Normalized 2048-byte/page full NAND image |
| `<TAG>.raw` | PTB partitions: `NBT`, `IPL`, `BAK`, `UDR`, `NK`, `DSK`, `CFG`, `END` |
| `NBT.code.bin` | nboot payload (page 1 onward of `NBT`) |
| `IPL.eboot.bin` | EBOOT payload from `IPL`, `IMG` wrapper stripped |
| `BAK.eboot.bin` | EBOOT payload from `BAK`, `IMG` wrapper stripped |
| `NK.binfs.raw` | BINFS sub-partition from the NK child partition table |
| `NK.fat.raw` | FAT sub-partition from the NK child partition table |
| `NK.ecec_NN.raw` | Individual ECEC images extracted from `NK.binfs.raw` |
| `NK.ecec_NN.modules/` | Rebuilt WinCE ROM module PE files |
| `nand_extract.json` | Geometry, PTB table, view provenance, ROMHDR tables, rebuild metadata |

The rebuilt PE files under `NK.ecec_NN.modules/` are analysis artifacts. They prioritize correct decompiler layout over loader-canonical fidelity and are not intended to execute in a WinCE environment.

See [docs/nk/](../../docs/nk/README.md) for detailed documentation of the NK partition structure, ECEC container format, ROM metadata layout, and module rebuild methodology.

## Acknowledgments

https://github.com/KodaSec/wince-decompr under the MIT License.
