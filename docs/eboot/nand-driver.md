# NAND Driver

EBOOT contains a full NAND flash driver that is significantly more capable
than the minimal NAND path in the bootrom. The bootrom loads nboot from
NAND using a simpler continuous-stream pattern. EBOOT's driver is table-
driven and re-issues a fresh READ sequence for each 512-byte chunk, which
is the pattern its own NAND boot loader and FMD layer expect.

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

For the normal data-read path, EBOOT programs:

```
dma_ctrl = (byte_count << 7) | 0x100018
```

Reading OOB/ECC instead of data uses `0x10001C`.

The page-program path in `nand_program_page` uses:

```
((528 - MEMORY[0x801076F8]) << 7) | 0x0C100012 | (MEMORY[0x801076E8] << 22) | 8
```

Another write-like path at `0x8006B28C` uses the same form with base
`0x0C100015`.

Observed status bits:

- bit 6 (`0x40`): transfer done, polled and then cleared by writing `1`
- bit 24 (`0x01000000`): additionally checked by `nand_program_page`
- bit 23 (`0x00800000`): additionally checked by the `0x0C100015` path

The exact meanings of the remaining bits are still unresolved.

### Sequencer Micro-Op Encoding

Each 32-bit word written to a sequencer slot `NF_SEQ_WORDn` encodes one
micro-op in the low 11 bits, with per-op arguments in bits `[21:11]`.
The opcodes directly confirmed in EBOOT are:

| Low 11 bits | Meaning                                       |
| ----------- | --------------------------------------------- |
| 0x62        | Output address byte, value in bits [21:11]    |
| 0x64        | Output command byte, value in bits [21:11]    |
| 0x119       | DMA transfer, byte count in bits [21:11]      |
| 0x129       | OOB/ECC transfer, byte count in bits [21:11]  |
| 0x401       | Wait / delay, tick count in bits [21:11]      |

This list is partial. Helper sequences also use additional opcodes such as
`0x59` and `0x201`, but their exact names are not yet decoded.

## Physical Page Layout: 528-Byte Chunks

In 2 KB-page mode, EBOOT treats the physical page as **four interleaved
data-plus-ECC chunks rather than as 2048 data bytes followed by 64 spare
bytes**:

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
with the ECC regions elided. In 4 KB-page mode, the same `512 + 16`
chunking scales to eight chunks per page.

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

The bootrom's `nf_read_chunk_to_buf` uses continuous-stream mode. From
the EBOOT-side assembly, what is directly established is only that the
bootrom and EBOOT use **different** read patterns; the exact mechanism
that lets the bootrom reconstruct its own image is outside the scope of
this document and should not be inferred from EBOOT alone.

**This difference is not documented anywhere in the bootrom material and
must be respected by any new code that talks to the NAND controller
directly.**

## Chip Database

`nand_detect_device` matches a 32-bit ID word against a hardcoded table at
`0x8003F5A0`. Each record is 36 bytes. The matched record feeds the rest of
the driver's runtime geometry, including page mode, chunks per page,
pages per block, column-byte count, row-byte count, and a packed timing
field at `+0x1C`.

The address-byte helper later emits exactly `record[0x0F]` column bytes
followed by `record[0x11]` row bytes.

## Driver Function Layer

### `nand_detect_device`

Top-level device bring-up. It first installs a caller-supplied I/O vector
into globals at `0x80103F4C..0x80103F64`, then does three fixed setup
steps:

- `pal_ioctl(0x01012020, {0x2000, 1}, 8, ...)`
- `pal_ioctl(0x010120EC, 44, 4, ...)`
- `0xA802A15C = 0x000F5AD1`, `0xA802A160 = 0x000F5C5C`,
  `0xA802B000 = 0x00010000`

It then probes up to two chip selects. If the caller requested reset, it
calls `nand_reset(cs)` first; it then calls `nand_init_chip(cs)`, compares
the returned 32-bit ID word against the 36-byte database, and accepts only
chips that match the same record.

On success it publishes the matched record, page mode
(`0/1/2` = `512/2048/4096` bytes), chunk count per page (`1/4/8`),
column-byte count, row-byte count, and additional device-derived
parameters used by the read/write paths. It also calls `sub_8006934C` on
the record's packed timing field at `+0x1C`.

### `nand_read_page(chip, row, col, dst, byte_count)`

Reads up to 512 bytes from logical offset `col` within page `row`.
`byte_count > 0x200` returns error `3`. Every call issues a fresh NAND
READ sequence, so `col` is treated as a logical data offset within the
page, not as a raw physical offset inside the 528-byte-interleaved stream.

In small-page mode (`MEMORY[0x801076A0] == 0`), the first command word is:

- `0x00000064` for `col < 0x100`
- `0x00000864` for `0x100 <= col < 0x200`
- `0x00028064` for `col >= 0x200`

In large-page modes, the sequence starts with `0x00000064`, emits the
column and row address bytes, then appends `0x00018464` (`cmd 0x30`).
The actual transfer is then driven by:

```
0xA802B000 = (byte_count << 7) | 0x100018
0xA802A100 = ((byte_count - 1) << 11) | 0x119
```

### `nand_read_oob_or_ecc(chip, row, col, dst, byte_count)`

Analogous to `nand_read_page` but targets the OOB/ECC path. It seeds the
sequence with `0x00040064`, sets bit 0 on the immediately preceding
sequencer word, then performs:

```
0xA802B000 = (byte_count << 7) | 0x10001C
0xA802A100 = ((byte_count - 1) << 11) | 0x129
```

`nand_verify_ecc_match` uses `col = 528 * N + 512` to read the per-chunk
OOB/ECC bytes back.

### `nand_program_page(chip, row, data, oob)`

Programs one logical page by looping over the runtime chunk count
(`MEMORY[0x801076A4]`). For each chunk it:

- programs

  ```
  0xA802B000 =
      ((528 - MEMORY[0x801076F8]) << 7)
    | 0x0C100012
    | (MEMORY[0x801076E8] << 22)
    | 8
  ```

- writes `0xA802A100 = 0x00107919`
- copies `512` bytes from `data + chunk * 512`
- copies `MEMORY[0x801076F4]` bytes from the caller-supplied `oob` pointer

`0x00107919` is still a `0x119` transfer opcode:

```
0x00107919 = ((528 - 1) << 11) | 0x119
```

The remaining bytes in each 528-byte physical chunk are controller-
generated. This is the other direct confirmation of the `512 + 16`
interleaving.

### `nand_read_id`, `nand_reset`, `nand_read_status`, `nand_cmd_sub`

These helpers are low-level controller wrappers, but the current function
names are not all accurate:

- `nand_reset` issues `cmd 0xFF` via `0x0007F864`, then waits with
  `0x00032401`
- `nand_read_id` is misnamed: it issues `cmd 0x70` via `0x00038064` and
  returns the low status byte from `0xA802A150`
- `nand_read_status` only does `0xA802A158 = (old & 0x7FFFF3FF) | value`
- `nand_cmd_sub` polls `0xA802A158[31]` until ready and calls the optional
  callback at `0x80103F4C` while waiting

### `nand_init_chip`

This is the actual Read ID helper used by `nand_detect_device`. It issues
`cmd 0x90` via `0x00048064`, emits one address byte `0x00` via `0x62`,
waits, performs a fixed-length readback, and returns the 32-bit value from
`0xA802A150`. `nand_detect_device` uses this return value as the chip-table
lookup key.

## NAND Boot Image Loader: `LoadNandBoot`

`LoadNandBoot` is the raw boot-image reader used by the boot-block
upgrade / verify helper `sub_80066564`. It is a general NAND boot-image
loader, not the normal flash `NK` boot path described in
[boot-flow.md](boot-flow.md).

`LoadNandBoot(dst, len)` first ORs `dst` with `0xA0000000`, reads
`row = 0, col = 0, len = 0x200`, then zero-fills the next `0x600` bytes.
It then continues from `row = 1`.

The remaining rows are read in 512-byte chunks according to the detected
page mode:

- mode `0`: one chunk per row at `col = 0`
- mode `1`: four chunks per row at `col = 0, 0x200, 0x400, 0x600`
- mode `2`: eight chunks per row at `col = 0 .. 0xE00` in `0x200` steps

Each chunk is fetched by a separate `nand_read_page` call. There is no
continuous-stream read in this loader.

Pseudo-code for the 2 KB-page variant:

```
nand_read_page(chip=0, row=0, col=0, dst=out, 0x200);
memset(out + 0x200, 0, 0x600);
out += 0x800;

for (page = 1; more_to_load; page++) {
    for (col = 0; col < 0x800; col += 0x200) {
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

Block status is handled by the FMD layer, not by `LoadNandBoot`.
`fmd_get_block_status`, `sub_8006C0F0`, `sub_8006C284`, and
`sub_8006C630` build and access a per-device status layout derived at
runtime by `sub_8006B7C4`.

What is directly verified:

- `LoadNandBoot` itself contains no bad-block-skip logic
- small-page and large-page devices use different block-status paths
- the small-page status probe performs repeated 1-byte reads through both
  `nand_read_oob_or_ecc` and `nand_read_page`

## Unresolved

- Exact bit meanings of the `0x100018` / `0x10001C` / `0x0C100012` /
  `0x0C100015` DMA control bases and of the writable fields in
  `0xA802A158`: not fully characterized.
- The second sequencer register block at `0x2002A100+`: used by EBOOT
  (the address-byte helper writes through it) but not independently
  documented.
- The complete sequencer opcode set is still only partially decoded.
  `0x62`, `0x64`, `0x119`, `0x129`, and `0x401` are confirmed; `0x59`
  and `0x201` are still unnamed.
- Hardware ECC strength: whether the 16-byte spare region per chunk
  implements 4-bit or 8-bit ECC, and how many bits are pure ECC vs
  metadata, is not determined.
- The packed timing field passed to `sub_8006934C` and expanded into
  `0x800F2408..0x800F241C` is not yet decoded at the bit level.
- The exact byte-level FMD block-status layout for small-page and
  large-page devices is not yet decoded.
