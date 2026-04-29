# ROMHDR and TOC

## ROMHDR

The `ROMHDR` is a standard WinCE ROM header structure, 0x54 bytes, consisting of 21 consecutive little-endian 32-bit fields. Its blob offset is:

```
blob_offset = ecec_pointer_to_offset(image, field_44)
```

where `field_44` comes from the ECEC image header (see [Partition and ECEC Layout](partition-and-ecec-layout.md)).

| Index | Field            | Notes                                          |
| ----: | ---------------- | ---------------------------------------------- |
|     0 | `dllfirst`       | lower bound of DLL virtual address range       |
|     1 | `dlllast`        | upper bound of DLL virtual address range       |
|     2 | `physfirst`      | first physical byte of ROM; equals `load_base` |
|     3 | `physlast`       | one past the last physical byte of ROM         |
|     4 | `nummods`        | ROM module count                               |
|     5 | `ram_start`      | WinCE RAM region start                         |
|     6 | `ram_free`       | initial free RAM pointer                       |
|     7 | `ram_end`        | WinCE RAM region end                           |
|     8 | `copy_entries`   | RAM copy entry count                           |
|     9 | `copy_offset`    | VA of RAM copy table                           |
|    10 | `profile_len`    | profiling buffer length                        |
|    11 | `profile_offset` | profiling buffer VA                            |
|    12 | `numfiles`       | ROM file count                                 |
|    13 | `kernel_flags`   | kernel feature flags                           |
|    14 | `fs_ram_percent` | filesystem RAM allocation percentage           |
|    15 | `drivglob_start` | driver global data VA                          |
|    16 | `drivglob_len`   | driver global data byte count                  |
|    17 | `cpu_type_misc`  | bits 15:0 = CPU type; bits 31:16 = misc flags  |
|    18 | `extensions`     | VA of ROM extensions structure                 |
|    19 | `tracking_start` | kernel debug tracking buffer VA                |
|    20 | `tracking_len`   | kernel debug tracking buffer byte count        |

CPU type `0x01C2` identifies ARM.

Observed values for the primary module images:

| Firmware | Image | `physfirst` | `physlast` | `nummods` | `numfiles` | CPU |
| --- | --- | --: | --: | --: | --: | --: |
| v1.58.2 | `ECEC_01` | `0x80600000` | `0x830B89A4` | 262 | 161 | `0x01C2` |
| v1.88 | `ECEC_01` | `0x80380000` | `0x82DD8348` | 262 | 161 | `0x01C2` |

Immediately following the `ROMHDR` are the ROM module table (`nummods` × 32 bytes) and ROM file table (`numfiles` × 28 bytes), in that order.

## ROM Module Table

Each entry is 32 bytes:

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| `+0x00` | 4 | `attributes` | WinCE module attribute flags |
| `+0x04` | 4 | `timestamp_low` | low 32 bits of file timestamp |
| `+0x08` | 4 | `timestamp_high` | high 32 bits of file timestamp |
| `+0x0C` | 4 | `size` | module byte size |
| `+0x10` | 4 | `name_pointer` | VA of null-terminated ASCII module name |
| `+0x14` | 4 | `e32_pointer` | VA of compact `e32_rom` header |
| `+0x18` | 4 | `o32_pointer` | VA of first compact `o32_rom` section descriptor |
| `+0x1C` | 4 | `load_pointer` | VA of the module's load address in the WinCE image |

`load_pointer` is the address at which WinCE places the module in the running system's virtual address space. It differs from `e32_rom.image_base`, which is the preferred compile-time base recorded in the compact ROM header.

EBOOT's second ECEC validation (`sub_8005B3E8`) walks this table and accepts the image only when one entry's `name_pointer` resolves to `nk.exe`.

## ROM File Table

Each entry is 28 bytes:

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| `+0x00` | 4 | `attributes` | WinCE file attribute flags |
| `+0x04` | 4 | `timestamp_low` | low 32 bits of file timestamp |
| `+0x08` | 4 | `timestamp_high` | high 32 bits of file timestamp |
| `+0x0C` | 4 | `size` | uncompressed file byte size |
| `+0x10` | 4 | `compressed_size` | compressed byte size; equals `size` when not compressed |
| `+0x14` | 4 | `name_pointer` | VA of null-terminated ASCII file name |
| `+0x18` | 4 | `load_pointer` | VA of file data in the ROM image |

ROM files are accessible from within the running WinCE system via the BinFS filesystem. ROM modules are not directly accessible as files; attempts to copy them silently fail.

## Compact e32_rom

The `e32_rom` header is 112 bytes (`0x70`), located at `ecec_pointer_to_offset(image, module.e32_pointer)`.

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| `+0x00` | 2 | `object_count` | number of `o32_rom` section descriptors |
| `+0x02` | 2 | `image_flags` | COFF characteristics |
| `+0x04` | 4 | `entry_rva` | entry point RVA |
| `+0x08` | 4 | `image_base` | preferred compile-time image base; **not** the WinCE load address |
| `+0x0C` | 2 | `subsystem_major` | PE subsystem major version |
| `+0x0E` | 2 | `subsystem_minor` | PE subsystem minor version |
| `+0x10` | 4 | `stack_max` | maximum stack size |
| `+0x14` | 4 | `virtual_size` | total virtual image byte count |
| `+0x18` | 4 | `sect14_rva` | [partial] purpose not fully determined |
| `+0x1C` | 4 | `sect14_size` | [partial] purpose not fully determined |
| `+0x20` | 4 | `timestamp` | image build timestamp |
| `+0x24` | 72 | `units[0..8]` | nine data directory entries, each 8 bytes: (RVA, size) |
| `+0x6C` | 2 | `subsystem` | PE subsystem identifier |
| `+0x6E` | 2 | (padding) |  |

Unit assignments:

| Index | Directory                                                  |
| ----- | ---------------------------------------------------------- |
| 0     | export directory                                           |
| 1     | import directory                                           |
| 2–8   | [partial] other directories; meanings not fully determined |

`image_base` records the address the module was linked for. Absolute pointers baked into section data reference this base. The actual WinCE load address is `module.load_pointer` from the ROM module table entry.

## Compact o32_rom

Each `o32_rom` section descriptor is 24 bytes (`0x18`). Descriptors begin at `ecec_pointer_to_offset(image, module.o32_pointer)` and are laid out consecutively for `e32_rom.object_count` entries.

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| `+0x00` | 4 | `virtual_size` | section virtual byte count |
| `+0x04` | 4 | `rva` | section RVA relative to `image_base`; page-aligned |
| `+0x08` | 4 | `physical_size` | section byte count in the stored image |
| `+0x0C` | 4 | `data_pointer` | VA of section bytes in the ECEC image |
| `+0x10` | 4 | `real_address` | [partial] runtime address; purpose not fully determined |
| `+0x14` | 4 | `flags` | section characteristic flags |

`data_pointer` is an independent virtual address for each section. It must be converted through `ecec_pointer_to_offset` separately for each section; sections are not assumed to be contiguous in the stored blob.

Common `flags` values observed:

| Value        | Meaning                                      |
| ------------ | -------------------------------------------- |
| `0x00000020` | executable code (`.text`)                    |
| `0x00000040` | initialized data (`.data`, `.rdata`)         |
| `0x00000080` | uninitialized data (`.bss`)                  |
| `0x00002000` | CECOMPRESS candidate; treated as compressed only when the section bytes pass block-header validation |

## Unresolved

- Full semantics of `e32_rom` directory units 2–8.
- The meaning of `sect14_rva` / `sect14_size` and how they relate to `o32_rom.real_address`.
- One module per ECEC image fails to resolve in each observed firmware version; the cause is not yet determined.
- Full extraction of ROM file table entries into usable files.
