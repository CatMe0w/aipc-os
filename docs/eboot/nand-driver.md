# NAND Driver

EBOOT contains a full NAND flash driver that is significantly more capable
than the minimal NAND path in the bootrom. The bootrom loads nboot from
NAND using a single probe-plus-continuous-stream pattern that works for
small images but cannot correctly read a 2 KB-page NAND in general. EBOOT's
driver uses a chip-database-driven, fresh-READ-per-chunk pattern that
correctly handles the physical layout of the NAND parts actually installed
on AIPC units.

See also [docs/bootrom/nand-boot.md](../bootrom/nand-boot.md) for the
bootrom-side NAND access primitives and register list. This document
focuses on the EBOOT driver layer and on the physical page layout.

## Controller Registers

The NAND hardware is split across two MMIO blocks (physical addresses):

| Base       | Range                  | Purpose                           |
| ---------- | ---------------------- | --------------------------------- |
| 0x2002A000 | 0x2002A000 - 0x2002A1FF| NAND flash command sequencer      |
| 0x2002B000 | 0x2002B000 - 0x2002B00F| NAND flash ECC/DMA control        |

The command sequencer at `0x2002A000..0x2002A058` is described in the
bootrom doc. The ECC/DMA control register at `0x2002B000` is the DMA
control word referenced throughout this document. A second sequencer block
exists at `0x2002A100+`; EBOOT uses it, the bootrom does not.

### DMA Control Register (0x2002B000)

EBOOT forms the DMA control register value for a chunk read as:

```
dma_ctrl = (byte_count << 7) | 0x100018
```

The bits encoded in the `0x100018` base:

- bit 3 and bit 4: transfer configuration / direction `[partial]`
- bit 20: mode selector `[partial]`
- bit 6 (not in the base): transfer-done flag, write-1-to-clear

Reading OOB instead of data uses `0x10001C` (bit 2 set) and a different
sequencer micro-op. Writing uses a different base altogether; see the
`nand_program_page` section below.

The exact per-bit semantics of `0x100018` are not fully characterized and
are marked `[partial]`.

### Sequencer Micro-Op Encoding

Each 32-bit word written to a sequencer slot `NF_SEQ_WORDn` encodes one
micro-op in the low 11 bits, with per-op arguments in bits `[21:11]`:

| Low 11 bits | Meaning                                       |
| ----------- | --------------------------------------------- |
| 0x62        | Output address byte, value in bits [21:11]    |
| 0x64        | Output command byte, value in bits [21:11]    |
| 0x119       | Read data, byte count in bits [21:11]         |
| 0x129       | Read OOB, byte count in bits [21:11]          |
| 0x401       | Wait / delay, tick count in bits [21:11]      |

The last four are distinct opcodes in the same encoding space. Writes to
the NAND buffer use yet another opcode whose exact bit pattern is embedded
in `nand_program_page` but not separately listed.

## Physical Page Layout: 4 x 528 Interleaved

The NAND parts AIPC ships with use a 2 KB data page with 64 bytes of
spare area, organized as **four interleaved data-plus-ECC chunks rather
than as 2048 data bytes followed by 64 spare bytes**:

```
offset  0 .. 511    data chunk 0    (512 bytes)
offset 512 .. 527   ECC chunk 0     (16 bytes)
offset 528 .. 1039  data chunk 1    (512 bytes)
offset 1040 .. 1055 ECC chunk 1     (16 bytes)
offset 1056 .. 1567 data chunk 2    (512 bytes)
offset 1568 .. 1583 ECC chunk 2     (16 bytes)
offset 1584 .. 2095 data chunk 3    (512 bytes)
offset 2096 .. 2111 ECC chunk 3     (16 bytes)

total                               2112 bytes per page
```

Each chunk is 528 bytes physical. The "logical" 2048-byte data area the
user cares about is the concatenation of the four 512-byte data regions,
with the ECC regions elided.

This layout is non-standard. Most NAND parts store the spare area as a
contiguous 64-byte region at the end of each page. The AK7802 NAND
controller instead interleaves the ECC bytes inline, one group after each
512-byte data block. Consumers that assume "data then spare" will read
garbage.

The interleaving is confirmed by an explicit `528` stride constant in
`nand_program_page` and by a corresponding `528 * N + 512` column address
formula used by the OOB read path when verifying a just-written page.

## Two Access Patterns

The NAND sequencer supports two behaviors for reading bytes after a READ
command has been issued, and the distinction is critical:

### Fresh-READ Mode

EBOOT's standard path re-issues a full NAND READ command sequence
(`cmd 0x00`, column address bytes, row address bytes, `cmd 0x30`, wait)
for every 512-byte chunk, each time with the column pointing at the
**start of the desired data chunk** in logical (not physical) coordinates.
Under this mode the NAND chip treats the column address as an offset into
the logical data area (2048 bytes), and the controller's hardware ECC
engine transparently skips the inline ECC bytes when delivering data to
the caller.

EBOOT's `nand_read_page` and `LoadNandBoot` both use this mode. It
delivers clean data with no interleaved ECC.

### Continuous-Stream Mode

Issuing a single READ command once per page and then pulling 512 bytes
at a time without re-issuing READ causes the sequencer to advance a
**physical** cursor through the raw 2112-byte stream. The hardware ECC
engine is active for each 512-byte chunk individually, but the caller
observes all 2112 physical bytes in order - including the 16-byte ECC
blocks between data chunks. The data delivered to the caller is the
raw stream, and reading `4 * 512 = 2048` bytes of it yields:

```
data chunk 0 (full)       +  ECC 0 + first 496 bytes of data chunk 1
data chunk 1 last 16 bytes +  ECC 1 + first 480 bytes of data chunk 2
data chunk 2 last 32 bytes +  ECC 2 + first 464 bytes of data chunk 3
```

...with the last 48 bytes of data chunk 3 never read at all.

The bootrom's `nf_read_chunk_to_buf` uses continuous-stream mode. It
works for loading nboot (which fits entirely in data chunk 0 of each
page) but cannot read a full 2 KB page correctly.

**This difference is not documented anywhere in the bootrom material and
must be respected by any new code that talks to the NAND controller
directly.**

## Chip Database

EBOOT detects the installed NAND chip via Read ID and looks the resulting
device ID up in a hardcoded chip database. Each database record is 36
bytes and includes the chip's geometry (column byte count, row byte count,
pages per block, page size, spare size, etc.). The database contains
entries for chips from multiple manufacturers including ST, Hynix,
Samsung, Toshiba, and Micron. All entries share the same driver logic;
only the geometry parameters differ.

The two AIPC units available for analysis both use Hynix NAND parts.
The exact Hynix model has not been cross-referenced against the Read ID
bytes on a live unit.

On a successful chip-database match, EBOOT stores the chip's column and
row byte counts in global variables that the address-byte helper uses to
emit the correct NAND address sequence. This is the source of the
per-chip address-cycle count: small 512 W parts need 3 or 4 bytes, larger
parts need 5.

## Driver Function Layer

### `nand_detect_device`

Top-level chip identification. Calls Read ID, walks the chip database,
stores the matching record's pointer to a global, and copies the chip's
column/row byte counts to the addressing-helper globals. Sets three
indirect function pointers that later helpers call through - one for
command issue, one for address emission, and one for data copy from the
L2 buffer.

This function also initializes the NF timing registers
`0x2002A15C` and `0x2002A160` with default values and writes `0x10000`
to the DMA control register `0x2002B000`.

### `nand_read_page(chip, row, col, dst, byte_count)`

Reads up to 512 bytes from logical offset `col` within page `row`.
`byte_count > 0x200` returns an error. Every call issues a fresh NAND
READ command sequence, so `col` is treated as a logical data offset
(0..2047 for a 2 KB page), not a physical offset.

The function chooses among `cmd 0x00`, `cmd 0x01`, or `cmd 0x50` for the
starting command byte depending on the column range, to support both
small-page and large-page chips. For large-page chips, `cmd 0x30` is
appended after the address bytes as the second command, triggering the
NAND's internal page-register load.

### `nand_read_oob_or_ecc(chip, row, col, dst, byte_count)`

Analogous to `nand_read_page` but targets the OOB/ECC region. Uses
sequencer micro-op `0x129` and DMA base `0x10001C` instead of `0x119`/
`0x100018`. The caller passes the column address with the `528 * N + 512`
formula to address the ECC region of chunk `N`.

### `nand_program_page(chip, row, data, oob)`

Writes a full page by looping over the chunk count stored in the chip
record. Each loop iteration writes `528 - spare_count` bytes of data
followed by the ECC bytes that the controller hardware generates. The
DMA control value for writing is:

```
((528 - spare_count) << 7) | 0xC100012 | (oob_toggle << 22) | 8
```

The low nibble differs from the read path's `0x100018` and enables the
hardware ECC generator.

### `nand_read_id`, `nand_reset`, `nand_read_status`, `nand_cmd_sub`

Straightforward wrappers that build short sequencer programs for the
corresponding single-byte NAND commands (Read ID `0x90`, Reset `0xFF`,
Read Status `0x70`, etc.).

### `nand_init_chip`

Per-chip bring-up: issues Reset, then Read ID, then populates chip
state. Called by `nand_detect_device` for each chip select (the driver
supports up to two chips).

## NAND Boot Path: `LoadNandBoot`

The function that loads the NK kernel image from NAND into DDR iterates
over source pages and calls `nand_read_page` four times per page with
`col = 0, 512, 1024, 1536` for a 2 KB-page chip (or more column steps
for 4 KB-page chips). Each call delivers a clean 512-byte chunk because
the fresh-READ-per-chunk pattern is applied consistently.

Pseudo-code for the 2 KB-page variant:

```
for (page = start_page; page < end_page; page++) {
    for (col = 0; col < page_size; col += 512) {
        nand_read_page(chip=0, row=page, col=col, dst=out, 0x200);
        out += 512;
    }
}
```

This is the reference implementation for "how to correctly read a 2 KB
page through the AK7802 NAND controller". Anyone writing a replacement
NAND loader (bootloader, Linux MTD driver, debug dumper) should follow
this pattern.

## Bad Block Handling

Bad block detection reads the OOB byte at page offset 1 of each block's
first page and declares the block bad if the byte is not `0xFF`. The
`nand_read_oob_or_ecc` path is used for this. This matches the
convention nboot uses (documented in `docs/nboot/boot-flow.md`).

EBOOT skips bad blocks when loading NK and prints the skipped block
index to UART (when UART is available). The maximum consecutive read
errors tolerated before aborting is hardcoded.

## Unresolved

- Exact bit meanings of the `0x100018` / `0x10001C` / `0xC100012` DMA
  control bases: not fully characterized.
- The second sequencer register block at `0x2002A100+`: used by EBOOT
  (the address-byte helper writes through it) but not independently
  documented.
- Hardware ECC strength: whether the 16-byte spare region per chunk
  implements 4-bit or 8-bit ECC, and how many bits are pure ECC vs
  metadata, is not determined.
- Exact chip identification for AIPC: the two test units use Hynix
  NAND, but the specific model and Read ID bytes have not been
  recorded.
- `nand_program_page` uses indirect function pointers set by
  `nand_detect_device` that were not independently traced; the OOB
  write sub-path is partially decompiled only.
