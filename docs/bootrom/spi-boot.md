# SPI Flash Boot Path

The SPI boot path (`probe_spi_boot_source`) is the first storage probe
attempted during normal boot. It uses the SPI controller at 0x20024000 to
read from an external SPI NOR flash.

## Probe Procedure

1. **Configure SPI controller**: `spi_boot_configure(0, 16, 0x15)` sets the
   SPI controller to: CS = 0, clock divider = 16, mode byte = 0x15
   (stored in bits [15:8] of SPI+0x00).
   SYSCTRL+0x78 bit 30 is also set to enable the SPI clock output sharepin.

2. **Iterate address byte counts** from 1 to 4. SPI NOR flashes use 1-4
   address bytes depending on density. The bootrom tries each width until
   it finds a valid header or exhausts all options.

3. **Read header prefix**: For each address byte count, issue
   `spi_boot_read(0, addr_bytes, L2BUF_01, 0x20)` to read the first 32 bytes
   from flash address 0.

4. **Check signature**: Extract 8 bytes starting at offset +0x04 within the
   read data (i.e., from L2BUF_01+0x10, which is 0x4800020C in memory) and
   compare against the ASCII string `"ANYKA382"`.

5. **Validate payload size**: On signature match, copy 2 dwords from the
   header tail area. If `payload_size` (first dword) is <= 0x20, reject
   (too small to be a real image) and continue iterating.

6. **Reconfigure SPI**: Apply the SPI configuration byte from the image
   header: `spi_boot_configure(0, 16, spi_tail.spi_cfg)`.

7. **Read full header**: Issue `spi_boot_read(0, addr_bytes, L2BUF_01, 0x118)`
   to read 0x118 (280) bytes from flash address 0, covering the complete
   boot header structure.

8. **Align payload size**: If `payload_size` is not 4-byte aligned, round up
   to the next multiple of 4.

9. **Dispatch by image type**:
   - **Type 6 (DDR)**: Execute the embedded register init script, then read
     the payload from flash offset 0x200 into DDR at 0x30000000. Return 2.
   - **Type 8 (L2)**: Read the payload from flash offset 0x200 into L2BUF_01
     at 0x48000200. Return 1.

10. If no valid image is found after trying all 4 address widths, return 0.

## SPI Read Protocol

`spi_boot_read(flash_addr, addr_byte_count, dst, byte_len)`:

1. Assert chip select: SPI+0x00 |= 0x22 (CS active + master enable).
2. Send command byte 0x03 (standard SPI READ).
3. Send `addr_byte_count` address bytes, MSB first:
   `flash_addr >> (8 * (count - 1))` down to `flash_addr >> 0`.
4. Deassert the write path: SPI+0x00 &= ~0x02.
5. Read `byte_len / 4` words via `spi_read_word()`:
   - Set SPI+0x00 bit 0 (read enable), set transfer count = 4, poll
     SPI+0x04 bit 8 for completion, clear read enable, return SPI+0x1C.
6. Deassert chip select: SPI+0x00 &= ~0x20, then SPI+0x00 |= 0x02.

## SPI Boot Image Header Layout

The full header occupies the first 0x200 bytes of the SPI flash. The payload
begins at flash offset 0x200.

```
Offset  Size   Field
0x00    16     Header prefix (first 4 dwords in L2BUF_01)
  0x00  4      [unverified - possibly version or flags]
  0x04  8      Signature: ASCII "ANYKA382" (packed as 2 × u32 LE)
  0x0C  4      [unverified]
0x10    8      SPI boot tail (copied as spi_tail)
  0x10  4      payload_size - byte count of the payload at offset 0x200
  0x14  4      spi_cfg | (other fields)
               Low byte [7:0] = SPI configuration byte for reconfigure
0x18    ...    Extended header body (total 0x10C bytes from offset 0x0C)
  ...   4      image_type - 6 = DDR image, 8 = L2 image
  ...   256    init_script - register init table (type 6 only),
               see boot-image-format.md
```

The exact offset of `image_type` and `init_script` within the extended header
depends on the structure packing. The decompiler treats the copy as 0x43 dwords
(268 bytes) starting from offset 0x0C of the L2 buffer data, placing
`image_type` and `init_script` within the `spi_boot_header_tail_t` structure
[unverified - field offsets need hardware validation].

## Return Values

| Value | Meaning                                                      |
| ----- | ------------------------------------------------------------ |
| 0     | No valid SPI flash image found                               |
| 1     | Type-8 image loaded to L2BUF_01 (0x48000200)                 |
| 2     | Type-6 image loaded to DDR (0x30000000), init script applied |
