# Module Rebuild

`tools/nand-extract` converts each ROM module descriptor into a PE file suitable for loading in decompilers. The goal is a correct analysis layout, not a byte-for-byte reconstruction of any original Platform Builder PE.

## Image Base

The rebuilt PE `ImageBase` is `module.load_pointer`, not `e32_rom.image_base`.

`e32_rom.image_base` is the preferred compile-time base at which the module was linked. At runtime, WinCE places the module at `module.load_pointer`. Absolute pointers baked into section data reference `image_base`, but the module executes at `load_pointer`. Using `image_base` as the PE base causes decompilers to report impossible-looking references; using `load_pointer` makes all internal cross-references land inside the rebuilt image.

Observed values for `SPI.dll`:

| Firmware | `e32_rom.image_base` | `load_pointer` (PE base) |
| -------- | -------------------: | -----------------------: |
| v1.58.2  |         `0x01710000` |             `0x82BE5000` |
| v1.88    |         `0x016F0000` |             `0x828E1000` |

## Section Bytes

Each section is copied independently from its own `o32_rom.data_pointer`:

```
blob_offset = ecec_pointer_to_offset(image, o32.data_pointer)
data        = image_blob[blob_offset : blob_offset + o32.physical_size]
```

Sections are not assumed to be contiguous. An `o32_rom.data_pointer` from one section may belong to a completely different scatter-load segment of the ECEC image than the module name string or the `e32_rom` header.

Section names assigned in the rebuilt PE:

| Condition | Name |
|---|---|
| `flags & 0x20` | `.text` |
| `flags & 0x80` | `.bss` |
| `flags & 0x40` and `flags & 0x80000000` | `.data` |
| `flags & 0x40` without `flags & 0x80000000` | `.rdata` |
| otherwise | `.secN` (N = descriptor index) |

This uses the normal PE read/write characteristic bits. It also covers `nk.exe`, whose `o32_rom` descriptors are not ordered as the usual `.text, .data, .rdata` sequence.

## In-Image Pointer Relocation

Section data may contain absolute pointers based on `e32_rom.image_base`. After copying each section, the extractor scans all 4-byte-aligned dwords and rewrites values in the range:

```
[e32.image_base,  e32.image_base + e32.virtual_size)
```

to:

```
module.load_pointer + (value - e32.image_base)
```

This converts stale compile-time addresses into the correct WinCE virtual addresses without touching MMIO constants or other unrelated dwords.

## Export Directory

The export directory RVA and size come from `e32_rom.units[0]`. When a valid export directory is present, the extractor copies the export blob into a synthetic `.edata` section, shifts all internal RVAs from their ROM positions to the new section's RVA, and sets the PE export data directory to point there.

This makes exported function names visible in decompilers without altering section byte layout.

Observed exports for `SPI.dll` in both firmware versions: `DllEntry`, `SPI_Close`, `SPI_Deinit`, `SPI_IOControl`, `SPI_Init`, `SPI_Open`, `SPI_Read`, `SPI_Seek`, `SPI_Write`.

## Import Directory

The import directory RVA and size come from `e32_rom.units[1]`. The rebuilt PE exposes this as its PE import data directory.

The descriptor area contains PE-like import descriptor records:

| Field                | Notes                       |
| -------------------- | --------------------------- |
| `OriginalFirstThunk` | RVA of ordinal thunk array  |
| `TimeDateStamp`      | zero in observed modules    |
| `ForwarderChain`     | zero in observed modules    |
| `Name`               | RVA of DLL name string      |
| `FirstThunk`         | RVA of IAT slots in `.data` |

Observed thunk arrays use PE ordinal import encoding:

```
thunk_value = 0x80000000 | ordinal
```

Decompilers use `OriginalFirstThunk` to name IAT slots and import wrapper functions.

The IAT slots in `.data` (`FirstThunk` entries) contain ROM-resolved runtime addresses, not disk-style ordinal thunks. Decompilers that require canonical disk PE import tables may not parse them correctly.

Observed import descriptors for `SPI.dll` in both firmware versions:

| DLL           | `OriginalFirstThunk` RVA | `Name` RVA | `FirstThunk` RVA |
| ------------- | -----------------------: | ---------: | ---------------: |
| `COREDLL.dll` |                 `0x23B0` |   `0x2410` |         `0x3014` |
| `CEDDK.dll`   |                 `0x239C` |   `0x241C` |         `0x3000` |

The ARM import wrapper pattern used throughout:

```asm
LDR R12, =__imp_FunctionName
LDR R12, [R12]
BX  R12
```

## Verified Result: SPI.dll (v1.88)

| Section | RVA | Virtual size |
|---|--:|--:|
| `.text` | `0x1000` | `0x1600` |
| `.data` | `0x3000` | `0x7C` |
| `.rdata` | `0x4000` | `0x108` |
| `.edata` | `0x6000` | `0xE4` |

| Item | Value |
|---|---|
| `ImageBase` | `0x828E1000` |
| Exports | 9 |
| Imports | 27 (from `COREDLL.dll` and `CEDDK.dll`) |

`SPI_Init` resolves to named WinCE API calls (`CreateFileW`, `DeviceIoControl`, `CloseHandle`, `OpenDeviceKey`, `RegQueryValueExW`, `RegCloseKey`, `MmMapIoSpace`) and maps physical register bases `0x20024000` (SPI0) and `0x20025000` (SPI1).

## Limits

- The rebuilt PE is an analysis artifact and is not intended to load in a running WinCE system.
- IAT slots contain ROM-resolved addresses rather than disk-style ordinal thunks.
- Sections whose `flags & 0x2000` (CECOMPRESS) are currently copied verbatim without decompression.
- Debug directories, relocation tables, and version resources are not reconstructed.

## Unresolved

- Decompressing sections with `IMAGE_SCN_COMPRESSED = 0x00002000` (CECOMPRESS algorithm).
- Rewriting `FirstThunk` into canonical ordinal thunks for tools that require strict PE import tables.
- Names and prototypes for unresolved CEDDK ordinals (e.g., ordinals 60 and 62 observed in `SPI.dll`).
