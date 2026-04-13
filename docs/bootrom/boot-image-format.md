# Boot Image Format

Both SPI and NAND boot paths expect images with a common header structure.
This document describes the shared format elements.

## Header Prefix

The first 0x0C bytes of the boot image (at flash offset 0) are the common
prefix. Within the L2 buffer after a read, this occupies
0x48000200..0x4800020B.

```
Offset  Size  Field
0x00    4     (header word 0) [unverified - possibly flags or version]
0x04    8     Signature: ASCII "ANYKA382" stored as two little-endian u32 words
```

Data from offset 0x0C onward is boot-medium-specific.

The signature bytes in memory appear as:

```
41 4E 59 4B 41 33 38 32   ("ANYKA382")
```

The string comparison function reads from a packed 32-bit word array,
extracting bytes as `word[i >> 2] >> (8 * (i & 3))` - consistent with
little-endian ARM byte ordering.

## SPI Boot Header Tail

For SPI flash images, the data starting at L2BUF_01 offset +0x0C
(address 0x4800020C) is copied into the `spi_boot_header_tail_t`
structure. The initial short read (0x20 bytes) extracts 2 dwords;
the full read (0x118 bytes) extracts 0x43 dwords (268 bytes).

Key fields within the tail structure:

| Relative Offset | Size | Field        | Description                                                                                                               |
| --------------- | ---- | ------------ | ------------------------------------------------------------------------------------------------------------------------- |
| +0x00           | 4    | payload_size | Byte count of the payload at flash offset 0x200. Must be > 0x20. If not 4-byte aligned, rounded up to next multiple of 4. |
| +0x04           | 4    | spi_cfg      | Low byte = SPI controller configuration byte (used to reconfigure the SPI clock/mode after the initial probe read)        |
| +0x08           | 4    | image_type   | 6 = DDR target, 8 = L2 target                                                                                             |
| +0x0C           | 256  | init_script  | Register init table (used only for type 6)                                                                                |

## NAND Boot Header Tail

For NAND flash images, the data starting at L2BUF_01 offset +0x0C is copied
into the `nf_boot_header_tail_t` structure. The initial read extracts 5
dwords; the full read extracts 0x46 dwords (280 bytes).

Key fields:

| Relative Offset | Size | Field                     | Description                                                 |
| --------------- | ---- | ------------------------- | ----------------------------------------------------------- |
| +0x00           | 4    | load_desc.counts          | Packed: chunks_per_page, page_count, cmd_count, dummy_count |
| +0x04           | 4    | load_desc.command         | Command bytes and address byte count                        |
| +0x08           | 4    | load_desc.timing_cfg0     | NF timing override 0 (0 = keep default)                     |
| +0x0C           | 4    | load_desc.timing_cfg1     | NF timing override 1 (0 = keep default)                     |
| +0x10           | 2    | load_desc.pre_delay_ticks | Delay before data read                                      |
| +0x12           | 2    | load_desc.seq_delay_ticks | Sequencer wait ticks                                        |
| +0x14           | 4    | image_type                | 6 = DDR target, 8 = L2 target                               |
| +0x18           | 256  | init_script               | Register init table (type 6 only)                           |

## Image Types

| Type | Target Address | Description                                                                                                                                                         |
| ---- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 6    | 0x30000000     | DDR image. The bootrom executes the embedded register init script (typically to initialize the DDR memory controller) before loading the payload into external RAM. |
| 8    | 0x48000200     | L2 image. Small payload loaded directly into L2 buffer SRAM. No init script is executed. Suitable for a second-stage loader that fits in ~5 KB.                     |

## Register Init Script

For type-6 images, the header contains a register initialization table
(`init_script`) that is processed by `apply_reg_init_script()` before the
payload is loaded. This is critical because DDR memory must be initialized
before it can be used as a load target.

The script is an array of {address, value} pairs, processed sequentially
up to 32 entries (64 words):

```
struct reg_init_op_t {
    uint32_t addr_or_tag;
    uint32_t value;
};
```

### Processing Rules

For each entry:

| addr_or_tag | Action                                                              |
| ----------- | ------------------------------------------------------------------- |
| 0x66668888  | **Delay**: call `delay_ticks(value)` - inserts a timed pause        |
| 0x88888888  | **End**: stop processing the script and return                      |
| (any other) | **Write**: write `value` to the memory-mapped address `addr_or_tag` |

### Example

```
{ <mmio_addr>, <value> }               - write register
{ 0x66668888, <tick_count> }           - delay
{ <mmio_addr>, <value> }               - write register
...
{ 0x88888888, 0x00000000 }             - end of script
```

The maximum script length is 32 entries (limited by the loop bound of
64 words / 2 words per entry). Exceeding this causes silent truncation -
the last entry may not be an end marker.

## Payload Layout

For both SPI and NAND:

- **SPI**: The payload begins at flash offset 0x200 and is `payload_size`
  bytes long.
- **NAND**: The payload begins at page 1 (the first page after the header
  page at page 0). The total size is determined by `page_count` and
  `chunks_per_page` from the load descriptor.

In both cases, the payload is the raw binary code that will be executed
at the target address (either 0x48000200 or 0x30000000). No additional
framing, compression, or checksumming is applied by the bootrom.
