# SPI Flash Boot Path

The SPI boot path (`probe_spi_boot_source`) is the first storage probe of a normal boot. It reads an external SPI NOR flash through the SPI controller at 0x20024000.

## Probe Procedure

1. **Configure the SPI controller**: `spi_boot_configure(0, 16, 0x15)` sets CS = 0, clock divider = 16, and mode byte = 0x15 in bits [15:8] of SPI+0x00. The bootrom also sets SYSCTRL+0x78 bit 30 to enable the SPI clock output sharepin.

2. **Step through the address byte counts** from 1 to 4. An SPI NOR flash takes 1 to 4 address bytes, which depends on its density. The bootrom tries each width until it finds a valid header or runs out of widths.

3. **Read the header prefix**: for each address byte count, call `spi_boot_read(0, addr_bytes, L2BUF_01, 0x20)` to read the first 32 bytes from flash address 0.

4. **Check the signature**: take the 8 bytes at offset +0x04 of the read data, which is L2BUF_01+0x04, or address 0x48000204. Compare them against the ASCII string `"ANYKA382"`.

5. **Validate the payload size**: on a signature match, copy 2 dwords from the header tail area. If `payload_size`, the first dword, is <= 0x20, reject the image as too small and continue to the next width.

6. **Configure the SPI controller again**: apply the SPI configuration byte from the image header with `spi_boot_configure(0, 16, spi_tail.spi_cfg)`.

7. **Read the full header**: call `spi_boot_read(0, addr_bytes, L2BUF_01, 0x118)` to read 0x118 bytes, 280 bytes, from flash address 0. This covers the complete boot header structure.

8. **Align the payload size**: if `payload_size` is not 4-byte aligned, round it up to the next multiple of 4.

9. **Dispatch by image type**:
   - **Type 6 (DDR)**: run the register init script from the header, then read the payload from flash offset 0x200 into DDR at 0x30000000. Return 2.
   - **Type 8 (L2)**: read the payload from flash offset 0x200 into L2BUF_01 at 0x48000200. Return 1.

10. If no width finds a valid image, return 0.

## SPI Read Protocol

`spi_boot_read(flash_addr, addr_byte_count, dst, byte_len)`:

1. Assert chip select: SPI+0x00 |= 0x22 (CS active and master enable).
2. Send command byte 0x03, the standard SPI READ.
3. Send `addr_byte_count` address bytes, MSB first, from `flash_addr >> (8 * (count - 1))` down to `flash_addr >> 0`.
4. Deassert the write path: SPI+0x00 &= ~0x02.
5. Read the data words in a loop. Set `j = 0`, then read one word with `spi_read_word()` and add 4 to `j` while `j < byte_len`. Each `spi_read_word()` call sets SPI+0x00 bit 0 (read enable) and sets the transfer count to 4. It then polls SPI+0x04 bit 8 for completion, clears read enable, and returns SPI+0x1C. A `byte_len` that is a multiple of 4 therefore gives `byte_len / 4` iterations. A `byte_len` that is not aligned, which does not occur in practice, reads one extra word.
6. Deassert chip select: SPI+0x00 &= ~0x20, then SPI+0x00 |= 0x02.

## SPI Boot Image Header Layout

The bootrom reads 0x118 bytes of header from flash offset 0. The payload starts at flash offset 0x200.

```
Offset  Size   Field
0x00    12     Header prefix
  0x00  4      [unverified - possibly version or flags]
  0x04  8      Signature: ASCII "ANYKA382" (packed as 2 x u32 LE)
0x0C    8      SPI boot tail (copied as spi_tail during the short read)
  0x0C  4      payload_size - byte count of the payload at offset 0x200
  0x10  4      spi_cfg | (other fields)
               Low byte [7:0] = SPI configuration byte for the reconfigure
0x14    4      image_type - 6 = DDR image, 8 = L2 image
0x18    256    init_script - register init table (type 6 only),
               see boot-image-format.md
```

## Return Values

| Value | Meaning                                                      |
| ----- | ------------------------------------------------------------ |
| 0     | No valid SPI flash image found                               |
| 1     | Type-8 image loaded to L2BUF_01 (0x48000200)                 |
| 2     | Type-6 image loaded to DDR (0x30000000), init script applied |
