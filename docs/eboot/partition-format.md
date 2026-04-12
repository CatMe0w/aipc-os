# Partition Format

The AIPC NAND does not use a standard WinCE `B000FF` NK image. Instead it
uses a vendor-specific partition table block called `PTB`, which sits in a
fixed location near the end of NAND and describes how the rest of the
device is carved up into labeled partitions. EBOOT reads the `PTB` to
locate the NK image (and everything else); nboot is unaware of it.

This document describes:

1. The on-NAND `PTB` block layout, including its page-level redundancy
2. The per-entry partition record format
3. The eight standard partition tags and their defaults
4. The `ECEC` sub-image container inside the `NK` partition

## PTB Block

A `PTB` block is **4 KB (one full two-page unit) stored at a NAND block
boundary**. On test units, the block sits near the end of NAND in the
second-to-last useful block (observed as NAND blocks 4086 and 4090 across
different machines - the block number is not fixed across devices).

A complete `PTB` block is a pair of independent `PTB` records, one per
2 KB NAND page, written by the factory programming tool for redundancy:

```
page 1 (offset 0x000 .. 0x7FF)     first PTB record
page 2 (offset 0x800 .. 0xFFF)     second PTB record, nearly identical
```

Each 2 KB half contains its own magic, header, padding, and entry table.
The two copies share the same entry data but carry different `record_num`
values (`1` and `2`) and different `page_num` values (the NAND page each
was physically programmed at). The two copies are almost byte-identical
aside from those self-identification fields.

The redundancy is page-level, not block-level: if the NAND block holding
the `PTB` goes bad, both copies are lost together. Cross-block redundancy
is not implemented.

### PTB Header (first 64 bytes of each 2 KB half)

| Offset | Size | Field          | Value on test units                |
| ------ | ---- | -------------- | ---------------------------------- |
| +0x00  | 4    | magic          | `"PTB\0"`                          |
| +0x04  | 4    | version string | `"01\0\0"`                         |
| +0x08  | 4    | page_num (u32) | NAND page number of this copy      |
| +0x0C  | 4    | record_num     | `1` for first copy, `2` for second |
| +0x10  | 4    | device IP      | `0x0B00A8C0` (LE = 192.168.0.11)   |
| +0x14  | 4    | subnet mask    | `0x00FFFFFF` (LE = 255.255.255.0)  |
| +0x18  | 4    | gateway IP     | `0` (disabled)                     |
| +0x1C  | 4    | padding        | `0`                                |
| +0x20  | 4    | unknown        | `0` `[partial]`                    |
| +0x24  | 4    | unknown        | `4` `[partial]`                    |
| +0x28  | 8    | unknown        | `0, 0` `[partial]`                 |
| +0x30  | 4    | unknown        | `0x000002C4` `[partial]`           |
| +0x34  | 4    | unknown        | `0x00000510` `[partial]`           |
| +0x38  | 8    | padding        | `0`                                |

The network configuration fields at `+0x10..+0x18` are the runtime source
of the active IP and subnet when EBOOT initiates a TFTP download. Those
values are **factory defaults baked into EBOOT** (see
[ethernet-driver.md](ethernet-driver.md) for the code path that writes
them), not user-configured data, although the on-NAND image reflects them
because the factory programming tool copies EBOOT's in-RAM default into
the NAND.

### Entry Table Placement

The entry table does not start at a fixed offset within the 2 KB half.
Observed values include `0x240` (576) and `0x6A0` (1696), and the offset
is allowed to change between firmware revisions and between factory
programming runs. A partition-extracting tool must locate the table by
scanning for the `"NBT\0"` tag (the `NBT` entry is always first) and then
parsing backward by 4 to recover the entry boundary.

Entry table layout:

```
entry[0]   NBT (nboot)           48 bytes
entry[1]   IPL (eboot)           48 bytes
entry[2]   BAK (eboot backup)    48 bytes
entry[3]   UDR (update record)   48 bytes
entry[4]   NK  (WinCE kernel)    48 bytes
entry[5]   DSK (filesystem)      48 bytes
entry[6]   CFG (config.txt)      48 bytes
entry[7]   END (sentinel)        48 bytes
```

The tag of the last entry is always `"END\0"`, and a parser should treat
that entry as a stop marker rather than as a real partition. Its
non-tag fields are not meaningful for extraction.

### Crossing the Page Boundary

If the table_offset is large enough that 8 * 48 = 384 bytes would run
off the end of the 2 KB half, the tail of the entry table spills over
into the second 2 KB half. When this happens, the tail bytes physically
overlap with the start of the second `PTB` header (the second page's
`"PTB\0"` magic and header fields). The first-half entry's tail is
therefore not independently interpretable - any parser that tries to
read the END entry's full 48-byte record will pick up the page 2
header data. The `END` tag itself still appears in the first-half copy
because the 4-byte tag field fits within page 1, so `find(..., "END\0")`
still lands correctly.

The second `PTB` copy is a full and independent record as long as its
own table_offset is not also large enough to overflow. If both copies
are usable, the second copy's entry table can be used as a cross-check
for the first.

## PTB Entry Record

Every entry is exactly `0x30 = 48` bytes:

| Offset | Size | Field                                                         |
| ------ | ---- | ------------------------------------------------------------- |
| +0x00  | 4    | `unk0` (u32) - typically 0; `NBT` sometimes holds 1 or 2      |
| +0x04  | 4    | tag (4 bytes ASCII, NUL-padded)                               |
| +0x08  | 16   | filename (ASCII, NUL-terminated)                              |
| +0x18  | 4    | padding (zero on all observed entries)                        |
| +0x1C  | 4    | flags (u32)                                                   |
| +0x20  | 4    | start_block (u32, NAND block index)                           |
| +0x24  | 4    | block_count (u32, block count)                                |
| +0x28  | 4    | load_addr (u32, physical load address, `0xFFFFFFFF` = none)   |
| +0x2C  | 4    | padding                                                       |

The exact meaning of `unk0` and of the `flags` bits is partially
understood; see the `Unresolved` section.

### Partition Tags

| Tag   | Typical filename | Meaning                                           |
| ----- | ---------------- | ------------------------------------------------- |
| `NBT` | `nboot.bin`      | First-stage bootloader (nboot)                    |
| `IPL` | `eboot.nb0`      | Second-stage bootloader (EBOOT itself)            |
| `BAK` | `eboot.bak`      | Backup copy of EBOOT                              |
| `UDR` | `nk.nb0`         | Update recovery / NK header stub `[partial]`      |
| `NK`  | `nk.nb0`         | Full WinCE NK image (as an `ECEC` container)      |
| `DSK` | `disk.img`       | Filesystem partition                              |
| `CFG` | `config.txt`     | Configuration text partition                      |
| `END` | `end.txt`        | Sentinel; marks end of table                      |

The `UDR` partition is always small (one block on observed units) and
shares the filename and load address with `NK`. Its exact role is
partial: plausibly a small recovery descriptor or a backup NK header,
but not independently verified.

### Default Layout on AIPC

A representative `PTB` from a v1.88 test unit:

| Tag | start_block | block_count | size      | load_addr   |
| --- | ----------- | ----------- | --------- | ----------- |
| NBT | 0           | 2           | 256 KB    | 0x00000000  |
| IPL | 2           | 8           | 1 MB      | 0x80038000  |
| BAK | 10          | 8           | 1 MB      | 0x80038000  |
| UDR | 18          | 1           | 128 KB    | 0x80200000  |
| NK  | 19          | 480         | 60 MB     | 0x80200000  |
| DSK | 499         | 1542        | 192.75 MB | 0xFFFFFFFF  |
| CFG | 2041        | 3           | 384 KB    | 0xFFFFFFFF  |
| END | -           | -           | -         | -           |

Block size is 128 KB. `load_addr = 0xFFFFFFFF` means the partition is
not loaded to memory; the partition is a file-system region read on
demand.

EBOOT itself loads at virtual `0x80038000` (physical `0x30038000`), and
the `IPL` and `BAK` entries match. The NK kernel loads at virtual
`0x80200000` (physical `0x30200000`).

## Default `PTB` Built by EBOOT

EBOOT includes a function that constructs the full default `PTB`
structure in RAM when no valid `PTB` is present on NAND. The
construction writes compile-time constants directly into the RAM copy,
and the resulting image is byte-identical (in the meaningful fields) to
what the factory programming tool writes to NAND on a fresh unit.

The network configuration fields inside the default `PTB` header are
filled in by a separate function that writes the factory-default IP and
mask literally - see `ptb_load_default_network_config` referenced from
[ethernet-driver.md](ethernet-driver.md). The `0xC0A8000B` / `0x00FFFFFF`
bytes observed in dumped `PTB` headers are compiled-in values, not data
that was stored independently in flash.

## WinCE Partition Types

Separate from the PTB tag system, the Flash Memory Device (FMD) layer
inside EBOOT uses a numeric partition type enum. A configuration string
in EBOOT lists the supported types:

```
1.extended; 2.DOS32; 3.BINFS; 4.XIP; 5.IMGFS;
```

| Type | Name     | Description                                  |
| ---- | -------- | -------------------------------------------- |
| 1    | extended | Logical partition container                  |
| 2    | DOS32    | FAT filesystem                               |
| 3    | BINFS    | WinCE native binary filesystem               |
| 4    | XIP      | Execute-In-Place (kernel image partition)     |
| 5    | IMGFS    | WinCE Image FileSystem (secondary storage)   |

The mapping between FMD partition types and PTB entry tags:

| FMD type | PTB tag | Role on AIPC                       |
| -------- | ------- | ---------------------------------- |
| 1 (boot) | NBT+IPL | Nand boot partition (NBOOT+EBOOT)  |
| 4 (XIP)  | NK      | WinCE kernel image                 |
| 5 (IMGFS)| DSK     | Filesystem / secondary storage     |

The "XIP" name is a legacy from NOR flash WinCE devices where the kernel
could execute directly from flash without being copied to RAM. On
NAND-based systems like AIPC, the kernel is loaded into DDR before
execution, but the partition type name persists. The maintenance menu's
"Format XIP disk" and "Update XIP" items operate on the NK partition
through this type mapping.

See [maintenance-mode.md](maintenance-mode.md) for the menu items that
exercise these partition types.

## NK Container Format: `ECEC` Sub-Images

The `NK` partition contents are **not** a standard WinCE `NK.bin`. There
is no `B000FF` magic. Instead the partition stores one or more `ECEC`
sub-images, each aligned to a 2 KB page boundary, followed by an optional
`@chain information` descriptor that ties them together.

### ECEC Sub-Image Header

Each sub-image begins with 64 bytes of pre-header data (boot stub or
module table header) followed by the `ECEC` signature and a short
header:

| Offset | Size | Field                                                      |
| ------ | ---- | ---------------------------------------------------------- |
| +0x00  | 64   | pre-header (boot stub / module table header, image-dependent) |
| +0x40  | 4    | magic `"ECEC"`                                             |
| +0x44  | 4    | end_address (u32): last byte address + 1                   |
| +0x48  | 4    | end_offset (u32): bytes from `load_base` to end            |
| +0x4C  | ..   | payload                                                    |

The load base of the sub-image is computed as:

```
load_base = end_address - end_offset
```

A valid header requires `end_address > end_offset`.

### Chain Information

When a single NK image is split into multiple `ECEC` sub-images (for
example when the kernel is larger than one contiguous region can hold),
the first sub-image includes a `"@chain information"` ASCII tag
somewhere in its payload. Close to that tag (within roughly 0x200 bytes
in either direction), there are 32-byte chain records that describe each
sub-image as pairs of (selector, load_base, span_size, flags).

A parser can discover each sub-image's span size by locating the chain
records and matching their `load_base` fields against the load bases
derived from the `ECEC` headers. Sub-images whose load base does not
appear in any chain record are treated as standalone images whose size
extends to the next sub-image header or to the end of the partition.

Typical AIPC NK containers hold two `ECEC` sub-images: a small header
image and a large companion image. Observed sizes from a test unit are
approximately 1.5 MB for the first and 56 MB for the second.

### Contents of the Sub-Images

Each sub-image contains standard WinCE ROM artifacts (ARM entry branch,
`APIS` table, kernel banner `"Windows CE Kernel for ARM"` in UTF-16,
`NK.EXE`, `COREDLL.dll`, module table markers, etc.). The details of
the WinCE ROM format itself are not specific to AIPC and are not
documented here.

## Unresolved

- The full meaning of header fields at `+0x20..+0x3F` inside the `PTB`
  header: values on test units are stable but their semantics are not
  determined. Candidate meanings include write count / generation,
  checksum, and boot-menu timeout.
- `PTB` entry `flags` field: bit meanings not determined. The bits
  plausibly encode partition type (boot / filesystem / config), ECC
  mode, and read-only status.
- `UDR` partition role: small (one block) and shares NK's filename and
  load address, but its function beyond "not the main NK" is not
  determined.
- `unk0` field at entry `+0x00`: observed to be 0 for most entries and
  1 or 2 for `NBT`. Possibly a per-entry version or retry counter.
- The `0xC0A8000B` `+0x10` field in the `PTB` header: a device IP on
  test units, but no evidence that factory tooling ever writes a
  different value. The field is readable but its editability in the
  field tooling is not verified.
- Second sequencer block at `0x2002A100+` is used when building entries;
  the interaction between the two sequencer blocks is outside this
  document.
