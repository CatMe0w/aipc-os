# NAND Driver

EBOOT holds a full NAND flash driver, and it can do much more than the minimal NAND path in the bootrom. The EBOOT driver is table-driven, and it issues a fresh READ sequence for every 512-byte chunk. Its own NAND boot loader and FMD layer expect that pattern. The bootrom and nboot each use a different combination of ECC state and READ granularity. [Column Addressing](#column-addressing) below compares all three.

See also [docs/bootrom/nand-boot.md](../bootrom/nand-boot.md) for the bootrom-side NAND access primitives and the register list. This document covers the EBOOT driver layer and the physical page layout.

## Controller Registers

The NAND hardware splits across two MMIO blocks, with these physical addresses:

| Base       | Range                   | Purpose                      |
| ---------- | ----------------------- | ---------------------------- |
| 0x2002A000 | 0x2002A000 - 0x2002A1FF | NAND flash command sequencer |
| 0x2002B000 | 0x2002B000 - 0x2002B01F | NAND flash ECC/DMA control   |

The bootrom document describes the command sequencer at `0x2002A000..0x2002A058`. The ECC/DMA control register at `0x2002B000` is the DMA control word throughout this document, and `0x2002B004` upward holds the ECC error-position array. A second sequencer block exists at `0x2002A100+`. EBOOT uses it, and the bootrom does not.

### DMA Control Register (0x2002B000)

This register controls both the NAND DMA transfer and the hardware BCH ECC engine. The decoded bit fields are:

| Bits | Mask | Name | Notes |
| --- | --- | --- | --- |
| 27 | 0x08000000 | RESULT_NO_OK | Status: errors present and not correctable |
| 26 | 0x04000000 | NO_ERR | Status: no errors found |
| 24 | 0x01000000 | DEC_RDY | Status: decode complete |
| 23 | 0x00800000 | ENC_RDY | Status: encode complete |
| 22 | 0x00400000 | ECC_MODE | 0 = 4-bit BCH (t=4), 1 = 8-bit BCH (t=8) |
| 20 | 0x00100000 | NFC_EN | Enable NFC DMA |
| 19:7 | 0x000FFF80 | BYTE_CFG | Byte count for ECC: `(reg >> 7) & 0x1FFF` |
| 6 | 0x00000040 | END | Transfer done; poll until set, then write 1 to clear |
| 5 | 0x00000020 | BIG_ENDIAN | 1 = big-endian bit order, 0 = little-endian |
| 4 | 0x00000010 | ADDR_CLR | Reset ECC address counter |
| 3 | 0x00000008 | START | Start transfer |
| 2 | 0x00000004 | DIR_WRITE | 1 = write direction, 0 = read |
| 1 | 0x00000002 | DEC_EN | Enable error correction (decode) |
| 0 | 0x00000001 | ENC_EN | Enable ECC generation (encode) |

An alternative naming of the same register exists elsewhere, and it agrees on every bit below 20. Above that it diverges. It places the ECC mode field at bit 21 rather than 22, and it names bit 24 the encode-ready flag and bit 25 the decode-ready flag. The table above follows this device instead, where the v1.88 nboot assembles the mode field as `mode << 22`, and every observed decoded read leaves bit 24 set. The divergence stays invisible in practice, because only mode 0 runs here, but it matters to anyone who extends this to 8-bit BCH.

For the normal data-read path, with no ECC, EBOOT programs:

```
dma_ctrl = (byte_count << 7) | 0x100018
```

`0x100018` is NFC_EN | ADDR_CLR | START. A read of OOB or ECC bytes instead of data uses `0x10001C`. That adds DIR_WRITE=1, or the OOB path selector. The exact interpretation is unresolved.

The ECC-assisted read path, decode mode, uses `BYTE_CFG = 521` and mode 0:

```
0x2002B000 = (521 << 7) | 0x0C100000 | ADDR_CLR | START | DEC_EN
           = 0x0C11049A
```

Hardware shows all three ECC outcomes after an ECC-decoded read. `0x059104C2` means a clean chunk. `0x019104C2` means errors that are correctable. `0x099104C2` means errors that are not. Correctable is the case where neither status bit is set.

The engine does not correct in place. On a correctable chunk the 512 data bytes that it delivers still hold the flipped bit, and software must apply the fix from the error-position registers.

### ECC Error-Position Registers (0x2002B004..)

After a decode that reports correctable errors, the engine writes one register per error position from `0x2002B004`, and it leaves the rest of the block at zero. The correction strength bounds the count, thus four registers for 4-bit BCH. Each word carries a 10-bit segment selector in bits [18:9] and a 9-bit position in bits [8:0]. [docs/nboot/boot-flow.md](../nboot/boot-flow.md) describes the decoder of nboot. On an uncorrectable chunk the engine still writes whatever its search produced.

The page-program path in `nand_program_page` uses:

```
((528 - ecc_bytes) << 7) | 0x0C100012 | (ecc_mode << 22) | START
```

where `ecc_bytes = 7` for mode 0, which gives `BYTE_CFG = 521`. Another write-like path at `0x8006B28C` uses the base `0x0C100015`.

### Sequencer Micro-Op Encoding

Each 32-bit word in a sequencer slot `NF_SEQ_WORDn` encodes one micro-op in the low 11 bits, with the per-op arguments in bits `[21:11]`. The opcodes that EBOOT confirms directly:

| Low 11 bits | Meaning                                      |
| ----------- | -------------------------------------------- |
| 0x62        | Output address byte, value in bits [21:11]   |
| 0x64        | Output command byte, value in bits [21:11]   |
| 0x119       | DMA transfer, byte count in bits [21:11]     |
| 0x129       | OOB/ECC transfer, byte count in bits [21:11] |
| 0x401       | Wait / delay, tick count in bits [21:11]     |

This list is partial. Helper sequences also use other opcodes, such as `0x59` and `0x201`, but their exact names are not yet decoded.

## Physical Page Layout: 528-Byte Chunks

In 2 KB-page mode, EBOOT treats the physical page as **four interleaved data-plus-ECC chunks, not as 2048 data bytes followed by 64 spare bytes**:

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

Each chunk is 528 physical bytes. The "logical" 2048-byte data area that the user cares about is the concatenation of the four 512-byte data regions, with the ECC regions left out. In 4 KB-page mode the same `512 + 16` chunking scales to eight chunks per page.

This layout is non-standard. Most NAND parts store the spare area as one contiguous 64-byte region at the end of each page. The AK7802 NAND controller interleaves the ECC bytes inline instead, one group after each 512-byte data block. A consumer that assumes "data then spare" reads garbage.

Two things confirm the interleaving: an explicit `528` stride constant in `nand_program_page`, and a matching `528 * N + 512` column address formula in the OOB read path that verifies a page just written.

## OOB Byte Layout and BCH ECC

Each 16-byte OOB region, the trailing bytes of each 528-byte physical chunk, holds three fields:

```
OOB[0:4]   4 bytes   user tag      written by software
OOB[4:9]   5 bytes   data mirror   hardware copies data[4:9] here automatically
OOB[9:16]  7 bytes   BCH parity    hardware computes and appends during write
```

The data mirror holds bytes 4 through 8 of the matching 512-byte data chunk. On a hardware-written page, `OOB[4:9] == data[4:9]` always holds.

The ECC engine implements binary BCH with these parameters:

- **Primitive polynomial**: 0x201b (GF(2^13), x^13 + x^4 + x^3 + x + 1)
- **Error correction capability**: t = 4 bits per protected region
- **Protected region**: 521 bytes, `data[0:512]` + `OOB[0:9]`, that is the data bytes followed by both the user tag and the data mirror
- **Parity output**: 7 bytes at `OOB[9:16]`, in the native BCH encoder output order, with no further byte or bit reordering

The BYTE_CFG field in the DMA control register carries the protected byte count. For mode 0, `BYTE_CFG = 528 - 7 = 521`.

To compute the ECC for a chunk in software:

```python
import bchlib
bch = bchlib.BCH(4, 0x201b)

def make_oob(data_512: bytes, tag_4: bytes) -> bytes:
    mirror = data_512[4:9]
    parity = bch.encode(data_512 + tag_4 + mirror)
    return tag_4 + mirror + parity   # 16 bytes

def make_raw_chunk(data_512: bytes, tag_4: bytes) -> bytes:
    return data_512 + make_oob(data_512, tag_4)   # 528 bytes
```

## Column Addressing

The column address is always a **physical** offset into the 2112-byte page. The controller does no logical-to-physical translation. Column `0x200` lands on the spare area of chunk 0, not on data chunk 1. Hardware confirms this: the ECC-disabled read sequence, `(len << 7) | 0x100018`, at columns `0`, `0x200` and `0x400` of an ECC-formatted page returns 512 bytes each, and all of them match a raw dump.

What changes is not the address space. It is whether the BCH engine sits in the data path.

### ECC Disabled

With `DEC_EN` clear, the bytes come back exactly as they sit in the page. This is how nboot finds the bad block markers, at physical columns `0x200` and `0x410`. It is also why the caller must do its own `528 * N + 512` arithmetic to reach the spare bytes of a chunk, which is what `nand_verify_ecc_match` does.

`nand_read_page` and `LoadNandBoot` use this mode. `LoadNandBoot` reads at columns `0`, `0x200`, `0x400` and `0x600`, and it gets clean contiguous data. The images that it reads are boot blocks, and they are written flat with no inline ECC at all. See [docs/nboot/boot-flow.md](../nboot/boot-flow.md). On an ECC-formatted partition the same call pattern would straddle the parity bytes.

### ECC Enabled

With `DEC_EN` set and `BYTE_CFG = 521`, the transfer micro-op requests 528 bytes per chunk. The engine consumes all 528, corrects over 521 of them, and delivers only the 512 data bytes to the caller. The sequencer cursor therefore advances one full physical chunk per transfer. One READ command followed by N transfers walks the page cleanly, and it never touches the column address again.

The page read of nboot does exactly this: one `cmd 0x00`, address, `cmd 0x30` per page, then four transfers of `((528 - 1) << 11) | 0x119`. Hardware reproduces it. The four resulting blocks match a 528-byte stride over their full length, and they concatenate into the ECC-stripped 2048-byte logical page. A 512-byte stride matches only the first chunk.

`nf_read_chunk_to_buf` in the bootrom is the third combination: ECC disabled, one READ per page, four 512-byte transfers. That walks the first 2048 physical bytes of the page, which is correct only for the flat-written boot blocks that it reads.

## Chip Database

`nand_detect_device` matches a 32-bit ID word against a hardcoded table at `0x8003F5A0`. Each record is 36 bytes. The matched record feeds the runtime geometry of the rest of the driver: the page mode, the chunks per page, the pages per block, the column-byte count, the row-byte count, and a packed timing field at `+0x1C`.

The address-byte helper later emits exactly `record[0x0F]` column bytes, then `record[0x11]` row bytes.

## Driver Function Layer

### `nand_detect_device`

Top-level device bring-up. It first installs a caller-supplied I/O vector into the globals at `0x80103F4C..0x80103F64`, then takes three fixed setup steps:

- `pal_ioctl(0x01012020, {0x2000, 1}, 8, ...)`
- `pal_ioctl(0x010120EC, 44, 4, ...)`
- `0xA802A15C = 0x000F5AD1`, `0xA802A160 = 0x000F5C5C`, `0xA802B000 = 0x00010000`

It then probes up to two chip selects. If the caller asked for a reset, it calls `nand_reset(cs)` first. It then calls `nand_init_chip(cs)`, compares the returned 32-bit ID word against the 36-byte database, and accepts only chips that match the same record.

On success it publishes the matched record, the page mode (`0/1/2` = `512/2048/4096` bytes), the chunk count per page (`1/4/8`), the column-byte count, the row-byte count, and further device-derived parameters for the read and write paths. It also calls `sub_8006934C` on the packed timing field of the record at `+0x1C`.

### `nand_read_page(chip, row, col, dst, byte_count)`

Reads up to 512 bytes from physical column `col` inside page `row`. A `byte_count > 0x200` returns error `3`. Every call issues a fresh NAND READ sequence. The ECC engine is not in the path, thus `col` addresses the raw 2112-byte page directly.

In small-page mode (`MEMORY[0x801076A0] == 0`), the first command word is:

- `0x00000064` for `col < 0x100`
- `0x00000864` for `0x100 <= col < 0x200`
- `0x00028064` for `col >= 0x200`

In the large-page modes, the sequence starts with `0x00000064`, emits the column and row address bytes, then appends `0x00018464` (`cmd 0x30`). These two writes then drive the transfer:

```
0xA802B000 = (byte_count << 7) | 0x100018
0xA802A100 = ((byte_count - 1) << 11) | 0x119
```

### `nand_read_oob_or_ecc(chip, row, col, dst, byte_count)`

The analogue of `nand_read_page` for the OOB and ECC path. It seeds the sequence with `0x00040064`, sets bit 0 on the sequencer word immediately before, then does:

```
0xA802B000 = (byte_count << 7) | 0x10001C
0xA802A100 = ((byte_count - 1) << 11) | 0x129
```

`nand_verify_ecc_match` uses `col = 528 * N + 512` to read the per-chunk OOB and ECC bytes back.

### `nand_program_page(chip, row, data, oob)`

Programs one logical page in a loop over the runtime chunk count (`MEMORY[0x801076A4]`). For each chunk it:

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

The controller generates the remaining bytes in each 528-byte physical chunk. This is the other direct confirmation of the `512 + 16` interleaving.

### `nand_read_id`, `nand_reset`, `nand_read_status`, `nand_cmd_sub`

These helpers are low-level controller wrappers, but not all of the current function names are accurate:

- `nand_reset` issues `cmd 0xFF` through `0x0007F864`, then waits with `0x00032401`
- `nand_read_id` carries the wrong name. It issues `cmd 0x70` through `0x00038064` and returns the low status byte from `0xA802A150`
- `nand_read_status` only does `0xA802A158 = (old & 0x7FFFF3FF) | value`
- `nand_cmd_sub` polls `0xA802A158[31]` until ready, and calls the optional callback at `0x80103F4C` while it waits

### `nand_init_chip`

This is the actual Read ID helper that `nand_detect_device` uses. It issues `cmd 0x90` through `0x00048064`, emits one address byte `0x00` through `0x62`, waits, does a fixed-length readback, and returns the 32-bit value from `0xA802A150`. `nand_detect_device` uses that return value as the chip-table lookup key.

## NAND Boot Image Loader: `LoadNandBoot`

`LoadNandBoot` is the raw boot-image reader of the boot-block upgrade and verify helper `sub_80066564`. It is a general NAND boot-image loader, not the normal flash `NK` boot path in [boot-flow.md](boot-flow.md).

`LoadNandBoot(dst, len)` first ORs `dst` with `0xA0000000`, reads `row = 0, col = 0, len = 0x200`, then zero-fills the next `0x600` bytes. It then continues from `row = 1`.

The remaining rows read in 512-byte chunks, and the detected page mode sets the pattern:

- mode `0`: one chunk per row at `col = 0`
- mode `1`: four chunks per row at `col = 0, 0x200, 0x400, 0x600`
- mode `2`: eight chunks per row at `col = 0 .. 0xE00`, in `0x200` steps

A separate `nand_read_page` call fetches each chunk, with the ECC engine disabled throughout.

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

This pattern is correct for the flat-written boot blocks that `LoadNandBoot` targets, where the four columns cover 2048 contiguous image bytes. It is **not** a template for a read of an ECC-formatted partition. With the ECC engine disabled, those columns straddle the inline parity bytes. For ECC-formatted data, follow the page read of nboot instead. It enables the BCH engine and lets the 528-byte transfer stride walk the page.

## Bad Block Handling

The FMD layer handles the block status, not `LoadNandBoot`. `fmd_get_block_status`, `sub_8006C0F0`, `sub_8006C284` and `sub_8006C630` build and access a per-device status layout that `sub_8006B7C4` derives at runtime.

Direct verification covers three points:

- `LoadNandBoot` itself holds no bad-block-skip logic
- small-page and large-page devices use different block-status paths
- the small-page status probe makes repeated 1-byte reads, through both `nand_read_oob_or_ecc` and `nand_read_page`

## Unresolved

- The meaning of the `0x10001C` DMA control base, the OOB and ECC read path, against `0x100018`, the data read path. The bit that distinguishes them is unconfirmed.
- The writable fields in `0xA802A158` are not fully characterized.
- The second sequencer register block at `0x2002A100+`. EBOOT uses it, because the address-byte helper writes through it, but no document covers it on its own.
- The complete sequencer opcode set is still only partly decoded. `0x62`, `0x64`, `0x119`, `0x129` and `0x401` are confirmed. `0x59` and `0x201` have no name yet.
- The packed timing field that goes to `sub_8006934C` and expands into `0x800F2408..0x800F241C` is not yet decoded at the bit level.
- The exact byte-level FMD block-status layout for small-page and large-page devices is not yet decoded.
