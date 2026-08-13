# Boot Image Format

The SPI and NAND boot paths both expect an image with a common header. This document describes the shared parts of that format.

## Header Prefix

The first 0x0C bytes of the boot image, at flash offset 0, are the common prefix. After a read into the L2 buffer, the prefix occupies 0x48000200..0x4800020B.

```
Offset  Size  Field
0x00    4     (header word 0) [unverified - possibly flags or version]
0x04    8     Signature: ASCII "ANYKA382" stored as two little-endian u32 words
```

The data from offset 0x0C onward depends on the boot medium.

The signature bytes in memory are:

```
41 4E 59 4B 41 33 38 32   ("ANYKA382")
```

`strcmp_l2_string` reads from a packed 32-bit word array. It extracts each byte as `word[i >> 2] >> (8 * (i & 3))`, which matches little-endian ARM byte order.

## SPI Boot Header Tail

For an SPI flash image, the bootrom copies the data at L2BUF_01 offset +0x0C (address 0x4800020C) into the `spi_boot_header_tail_t` structure. The short probe read of 0x20 bytes gives 2 dwords. The full read of 0x118 bytes gives 0x43 dwords, or 268 bytes.

Key fields in the tail structure:

| Relative Offset | Size | Field | Description |
| --- | --- | --- | --- |
| +0x00 | 4 | payload_size | Byte count of the payload at flash offset 0x200. Must be > 0x20. A value that is not 4-byte aligned rounds up to the next multiple of 4. |
| +0x04 | 4 | spi_cfg | Low byte = SPI controller configuration byte. The bootrom uses it to set the SPI clock and mode again after the probe read. |
| +0x08 | 4 | image_type | 6 = DDR target, 8 = L2 target |
| +0x0C | 256 | init_script | Register init table, type 6 only |

## NAND Boot Header Tail

For a NAND flash image, the bootrom copies the data at L2BUF_01 offset +0x0C into the `nf_boot_header_tail_t` structure. The probe read gives 5 dwords. The full read gives 0x46 dwords, or 280 bytes.

Key fields:

| Relative Offset | Size | Field | Description |
| --- | --- | --- | --- |
| +0x00 | 4 | load_desc.counts | Packed: chunks_per_page, page_count, cmd_count, dummy_count |
| +0x04 | 4 | load_desc.command | Command bytes and address byte count |
| +0x08 | 4 | load_desc.timing_cfg0 | NF timing override 0 (0 = keep default) |
| +0x0C | 4 | load_desc.timing_cfg1 | NF timing override 1 (0 = keep default) |
| +0x10 | 2 | load_desc.pre_delay_ticks | Delay before data read |
| +0x12 | 2 | load_desc.seq_delay_ticks | Sequencer wait ticks |
| +0x14 | 4 | image_type | 6 = DDR target, 8 = L2 target |
| +0x18 | 256 | init_script | Register init table, type 6 only |

## Image Types

| Type | Target Address | Description |
| --- | --- | --- |
| 6 | 0x30000000 | DDR image. The bootrom runs the register init script in the header, usually to set up the DDR memory controller, then loads the payload into external RAM. |
| 8 | 0x48000200 | L2 image. The bootrom loads a small payload straight into L2 buffer SRAM and runs no init script. This suits a second-stage loader of about 5 KB or less. |

## Register Init Script

For a type-6 image, the header carries a register init table (`init_script`). `apply_reg_init_script()` runs the table before the payload load. This matters because DDR memory needs initialization before it can hold the payload.

The script is an array of {address, value} pairs. The bootrom runs them in order, up to 32 entries, or 64 words:

```
struct reg_init_op_t {
    uint32_t addr_or_tag;
    uint32_t value;
};
```

### Processing Rules

For each entry:

| addr_or_tag | Action |
| --- | --- |
| 0x66668888 | **Delay**: call `delay_ticks(value)` for a timed pause |
| 0x88888888 | **End**: stop the script and return |
| (any other) | **Write**: write `value` to the memory-mapped address `addr_or_tag` |

### Example

```
{ <mmio_addr>, <value> }               - write register
{ 0x66668888, <tick_count> }           - delay
{ <mmio_addr>, <value> }               - write register
...
{ 0x88888888, 0x00000000 }             - end of script
```

The loop bound of 64 words, or 2 words per entry, limits the script to 32 entries. A longer script truncates without a message, and the last entry it runs can be something other than the end marker.

## Payload Layout

For SPI and NAND both:

- **SPI**: the payload starts at flash offset 0x200 and is `payload_size` bytes long.
- **NAND**: the payload starts at page 1, the first page after the header page at page 0. `page_count` and `chunks_per_page` in the load descriptor give the total size.

In both cases the payload is the raw binary code that runs at the target address, either 0x48000200 or 0x30000000. The bootrom adds no framing, no compression and no checksum.
