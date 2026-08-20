# Module Rebuild

`tools/aipc-nand-extract` converts each ROM module descriptor into a PE file that a decompiler can load. The goal is a correct analysis layout, not a byte-for-byte reconstruction of an original Platform Builder PE.

## Image Base

The `ImageBase` of the rebuilt PE is `module.load_pointer`, not `e32_rom.image_base`.

`e32_rom.image_base` is the preferred compile-time base that the module was linked for. At runtime WinCE places the module at `module.load_pointer`. Absolute pointers inside the section data refer to `image_base`, but the module executes at `load_pointer`. With `image_base` as the PE base, a decompiler reports references that look impossible. With `load_pointer`, every internal cross-reference lands inside the rebuilt image.

Observed values for `SPI.dll`:

| Firmware | `e32_rom.image_base` | `load_pointer` (PE base) |
| -------- | -------------------: | -----------------------: |
| v1.58.2  |         `0x01710000` |             `0x82BE5000` |
| v1.88    |         `0x016F0000` |             `0x828E1000` |

## Section Bytes

Each section comes from its own `o32_rom.data_pointer`:

```
blob_offset = ecec_pointer_to_offset(image, o32.data_pointer)
data        = image_blob[blob_offset : blob_offset + o32.physical_size]
```

Do not assume that the sections are contiguous. The `o32_rom.data_pointer` of one section can belong to a completely different scatter-load segment of the ECEC image than the module name string or the `e32_rom` header.

When `flags & 0x2000` is set, and the stored bytes start with a valid CECOMPRESS block table, the extractor decompresses the section before it writes the rebuilt PE. The CECOMPRESS wrapper holds a 24-bit uncompressed size, then 24-bit block-end offsets. Each block has a 16-byte ROM-LZX header:

```
u32 window_bits        // observed: 16
u32 uncompressed_size  // normally 0x1000, shorter for the final block
u32 compressed_size
u32 uncompressed_size_copy
```

The rebuilt PE section keeps the original `o32_rom.virtual_size`, writes the decompressed bytes as raw section data, and clears the `0x2000` compressed flag from the PE section characteristics. Some sections carry `0x2000` even though their stored bytes fail the CECOMPRESS block-header checks. The extractor leaves those sections alone rather than force them through the decompressor.

Section names in the rebuilt PE:

| Condition                                   | Name                           |
| ------------------------------------------- | ------------------------------ |
| `flags & 0x20`                              | `.text`                        |
| `flags & 0x80`                              | `.bss`                         |
| `flags & 0x40` and `flags & 0x80000000`     | `.data`                        |
| `flags & 0x40` without `flags & 0x80000000` | `.rdata`                       |
| otherwise                                   | `.secN` (N = descriptor index) |

This uses the normal PE read and write characteristic bits. It also covers `nk.exe`, whose `o32_rom` descriptors do not follow the usual `.text, .data, .rdata` order.

## In-Image Pointer Relocation

Section data can hold absolute pointers based on `e32_rom.image_base`. After it copies each section, the extractor scans all 4-byte-aligned dwords and rewrites every value in this range:

```
[e32.image_base,  e32.image_base + e32.virtual_size)
```

to:

```
module.load_pointer + (value - e32.image_base)
```

This turns a stale compile-time address into the correct WinCE virtual address, and it leaves MMIO constants and other unrelated dwords alone.

## Export Directory

The export directory RVA and size come from `e32_rom.units[0]`. Where a valid export directory exists, the extractor copies the export blob into a synthetic `.edata` section, shifts every internal RVA from its ROM position to the RVA of the new section, and points the PE export data directory there.

This makes the exported function names visible in a decompiler without a change to the section byte layout.

Observed exports for `SPI.dll` in both firmware versions: `DllEntry`, `SPI_Close`, `SPI_Deinit`, `SPI_IOControl`, `SPI_Init`, `SPI_Open`, `SPI_Read`, `SPI_Seek`, `SPI_Write`.

## Import Directory

The import directory RVA and size come from `e32_rom.units[1]`. The rebuilt PE exposes this as its PE import data directory.

The descriptor area holds PE-like import descriptor records:

| Field                | Notes                       |
| -------------------- | --------------------------- |
| `OriginalFirstThunk` | RVA of ordinal thunk array  |
| `TimeDateStamp`      | zero in observed modules    |
| `ForwarderChain`     | zero in observed modules    |
| `Name`               | RVA of DLL name string      |
| `FirstThunk`         | RVA of IAT slots in `.data` |

The thunk arrays use PE ordinal import encoding:

```
thunk_value = 0x80000000 | ordinal
```

A decompiler uses `OriginalFirstThunk` to name the IAT slots and the import wrapper functions.

The IAT slots in `.data`, the `FirstThunk` entries, hold ROM-resolved runtime addresses, not disk-style ordinal thunks. A decompiler that needs a canonical disk PE import table can fail to parse them.

Observed import descriptors for `SPI.dll` in both firmware versions:

| DLL           | `OriginalFirstThunk` RVA | `Name` RVA | `FirstThunk` RVA |
| ------------- | -----------------------: | ---------: | ---------------: |
| `COREDLL.dll` |                 `0x23B0` |   `0x2410` |         `0x3014` |
| `CEDDK.dll`   |                 `0x239C` |   `0x241C` |         `0x3000` |

The ARM import wrapper pattern throughout:

```asm
LDR R12, =__imp_FunctionName
LDR R12, [R12]
BX  R12
```

## Verified Result: SPI.dll (v1.88)

| Section  |      RVA | Virtual size |
| -------- | -------: | -----------: |
| `.text`  | `0x1000` |     `0x1600` |
| `.data`  | `0x3000` |       `0x7C` |
| `.rdata` | `0x4000` |      `0x108` |
| `.edata` | `0x6000` |       `0xE4` |

| Item        | Value                                   |
| ----------- | --------------------------------------- |
| `ImageBase` | `0x828E1000`                            |
| Exports     | 9                                       |
| Imports     | 27 (from `COREDLL.dll` and `CEDDK.dll`) |

`SPI_Init` resolves to named WinCE API calls (`CreateFileW`, `DeviceIoControl`, `CloseHandle`, `OpenDeviceKey`, `RegQueryValueExW`, `RegCloseKey`, `MmMapIoSpace`). It maps the physical register bases `0x20024000` (SPI0) and `0x20025000` (SPI1).

## Limits

- The rebuilt PE is an analysis artifact. It is not for a running WinCE system.
- The IAT slots hold ROM-resolved addresses, not disk-style ordinal thunks.
- The extractor does not reconstruct debug directories, relocation tables or version resources.

## Unresolved

- A rewrite of `FirstThunk` into canonical ordinal thunks, for tools that need a strict PE import table.
- Names and prototypes for the unresolved CEDDK ordinals, for example ordinals 60 and 62 in `SPI.dll`.
