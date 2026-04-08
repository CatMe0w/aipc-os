# aipc-nand-extract

Extract boot images from an AIPC netbook WinCE NAND dump produced by `nand-dump`
or `nand-dump-min`.

## Usage

```
uv run aipc-nand-extract nand.img -o out/
```

Without `-o`, files are written to an `extracted/` directory next to the
input image.

## NAND partition layout

| Offset       | Size   | Content                                      |
| ------------ | ------ | -------------------------------------------- |
| `0x00000000` | 128 KB | nboot (ANYKA382 type-6 DDR image)            |
| `0x00040000` | 512 KB | eboot IPL (Anyka IMG wrapper + EBOOT binary) |
| `0x00240000` | 512 KB | eboot BAK (backup copy)                      |
| `0x00480000` | ~68 MB | NK / WinCE OS image region                   |
| `0x1FF60000` | 4 KB   | PTB (Partition Table Block)                  |

## Output files

| File                 | Format   | Description                                                                         |
| -------------------- | -------- | ----------------------------------------------------------------------------------- |
| `nboot.nb0`          | raw ARM  | nboot payload, loaded to `0x30000000` by the bootrom. Initializes DDR, loads eboot. |
| `nboot.akimg`        | Anyka    | Full ANYKA382 image (bootrom header + register init script + payload).              |
| `nboot_ddr_init.txt` | text     | Human-readable DDR register init sequence extracted from the ANYKA382 header.       |
| `eboot.nb0`          | raw ARM  | EBOOT binary with Anyka IMG header stripped. Same name the vendor uses.             |
| `eboot.akimg`        | Anyka    | Full Anyka IMG region (`IMG\0` header + binary).                                    |
| `eboot_bak.nb0`      | raw ARM  | Backup EBOOT (typically identical to `eboot.nb0`).                                  |
| `eboot_bak.akimg`    | Anyka    | Backup Anyka IMG region.                                                            |
| `nk.raw`             | raw dump | NK / OS region, raw NAND content. Not standard WinCE BIN format.                    |
| `ptb.bin`            | binary   | Partition Table Block (Anyka config data).                                          |

### File format notes

- **`.nb0`** is the standard WinCE naming for a raw binary NAND boot image -
  flat ARM code with no container, suitable for direct loading or
  disassembly.
- **`.akimg`** is a project-local convention for Anyka proprietary image
  wrappers (ANYKA382 bootrom format or `IMG\0` envelope). These contain
  metadata (signatures, load descriptors, register init scripts) that the
  raw `.nb0` files do not.
- **`nk.raw`** is deliberately not named `NK.bin` because the region does
  not use the standard WinCE BIN format (`B000FF\n` header + address/data
  records). The eboot loads it via an Anyka-specific mechanism.
