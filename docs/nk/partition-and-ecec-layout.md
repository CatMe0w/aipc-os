# Partition and ECEC Layout

## NK Child Partition Table

The first 2048-byte page of the `NK` partition is an MBR-style sector. The WinCE partition manager uses it to locate the BINFS and FAT sub-regions.

| Field          | Offset  | Size | Notes                                     |
| -------------- | ------- | ---- | ----------------------------------------- |
| boot indicator | `+0x00` | 1    | opaque; not a standard MBR boot indicator |
| partition type | `+0x04` | 1    | `0x21` = BINFS, `0x04` = FAT              |
| `lba_start`    | `+0x08` | 4    | first sector of sub-partition (LE uint32) |
| `sector_count` | `+0x0C` | 4    | sector count (LE uint32)                  |

The table starts at offset `0x1BE` in the first page and holds four 16-byte entries. Signature `0x55AA` sits at offset `0x1FE`. The sector unit is 2048 bytes, one clean NAND page, thus:

```
sub-partition offset = lba_start    * 0x800
sub-partition size   = sector_count * 0x800
```

Both firmware versions show these sub-partitions:

| Sub-partition  | Type   | Notes                                       |
| -------------- | ------ | ------------------------------------------- |
| `NK.binfs.raw` | `0x21` | Contains ECEC images and WinCE ROM metadata |
| `NK.fat.raw`   | `0x04` | Filesystem storage area                     |

## ECEC Images

`NK.binfs.raw` starts with one or more ECEC images, back to back. The sub-partition can hold trailing space after the span of the last ECEC image. Each image starts on a page boundary, and the magic `ECEC` at raw offset `+0x40` from the image start identifies it. The 64 bytes before the magic, `+0x00..+0x3F`, hold 16 little-endian DWORDs that we do not yet interpret.

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| `+0x40` | 4 | magic | `"ECEC"` |
| `+0x44` | 4 | `field_44` | Virtual address of the image `ROMHDR` |
| `+0x48` | 4 | `field_48` | `field_44 - load_base`, the logical offset of `ROMHDR` from `physfirst` |

```
load_base = field_44 - field_48
```

`load_base` equals `ROMHDR.physfirst`. Every virtual address in the ROM metadata is an offset from this base.

Observed ECEC images:

| Firmware | Image     | Start in `NK.binfs.raw` |         Span |  `load_base` |
| -------- | --------- | ----------------------: | -----------: | -----------: |
| v1.58.2  | `ECEC_00` |            `0x00000000` | `0x00410000` | `0x80200000` |
| v1.58.2  | `ECEC_01` |            `0x00410000` | `0x035D7800` | `0x80600000` |
| v1.88    | `ECEC_00` |            `0x00000000` | `0x00183000` | `0x80200000` |
| v1.88    | `ECEC_01` |            `0x00183000` | `0x037F0000` | `0x80380000` |

The span is larger than the logical image size, because a stored ECEC image carries periodic metadata pages that are not part of the WinCE virtual address space.

## Metadata Pages

An ECEC image carries one metadata page per fixed number of stored pages. A conversion from a virtual address to a blob offset must skip the metadata pages.

A stored page is a metadata page when:

- bytes `+0x04..+0x07` equal `0xFFFBFFFD`
- bytes `+0x08..+0x0F` equal `0xFFFFFFFF`
- more than half of the 2048-byte page holds `0xFF`

Metadata pages appear at a fixed global period across all of `NK.binfs.raw`. The period is the GCD of the intervals between consecutive metadata page indices. In each group of `raw_pages_per_group` consecutive stored pages, the metadata page always sits at the same index, `metadata_page_mod`.

```
raw_pages_per_group   = period
logical_pages_per_group = period - 1
```

Observed global layout:

| Firmware | `raw_pages_per_group` | `logical_pages_per_group` | `metadata_page_mod` |
| --- | --: | --: | --: |
| v1.58.2 | 64 | 63 | 62 |
| v1.88 | 128 | 127 | 126 |

Each ECEC image has its own local `metadata_page_index`, which comes from the global layout and the start position of the image:

```
image_start_page       = image_start_offset / 0x800
metadata_page_index    = (metadata_page_mod - image_start_page) % raw_pages_per_group
```

Observed per-image metadata page indices:

| Firmware | `ECEC_00` index | `ECEC_01` index |
| -------- | --------------: | --------------: |
| v1.58.2  |              62 |              30 |
| v1.88    |             126 |             120 |

## Logical-to-Raw-Offset Formula

To convert a logical offset, the virtual address minus `load_base`, into a blob offset in the ECEC image file:

```
logical_page, page_offset = divmod(logical_offset, 0x800)
group, page_in_group      = divmod(logical_page, logical_pages_per_group)
raw_page_in_group         = page_in_group + (1 if page_in_group >= metadata_page_index else 0)
blob_offset               = (group * raw_pages_per_group + raw_page_in_group) * 0x800 + page_offset
```

To convert a virtual address pointer into a blob offset:

```
logical_offset = pointer - image.load_base
blob_offset    = ecec_logical_to_raw_offset(logical_offset)
```

Every ROM metadata pointer uses this mapping: `name_pointer`, `e32_pointer`, `o32_pointer`, `data_pointer`, and the `ROMHDR` pointer at `field_44`.

## Chain Information Record

A `chain information` table in `NK.binfs.raw` records the logical size of each ECEC image. To locate the table, search the blob of the first ECEC image for the ASCII string `"chain information\0"`. Records near that anchor have this format:

| Field          | Size | Notes                                             |
| -------------- | ---- | ------------------------------------------------- |
| `load_base`    | 4    | must match a known ECEC `load_base`               |
| `logical_size` | 4    | page-aligned logical byte count for that image    |
| `order`        | 4    | high word must be `1`; low word is sequence index |
| `reserved`     | 4    | must be `0`                                       |

Where the chain information exists, its `logical_size` is the authoritative size for each image. It takes precedence over any size inferred from the span between consecutive ECEC headers.

Observed logical sizes:

| Firmware |  `load_base` | Logical size |
| -------- | -----------: | -----------: |
| v1.58.2  | `0x80200000` | `0x00400000` |
| v1.58.2  | `0x80600000` | `0x03500000` |
| v1.88    | `0x80200000` | `0x00180000` |
| v1.88    | `0x80380000` | `0x03780000` |

## Unresolved

- The meaning of the 16 DWORDs at `+0x00..+0x3F`, before the `ECEC` magic.
- The content and the purpose of the metadata pages, beyond the marker dwords that we recognize.
- The full structure of the MBR page (`NK.raw[0x000..0x7FF]`). Only the partition table at `+0x1BE` and the signature at `+0x1FE` are decoded. The 446 bytes before them are unexamined.
