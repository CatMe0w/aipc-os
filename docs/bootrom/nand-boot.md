# NAND Flash Boot Path

The NAND flash boot path (`probe_flash_boot_source`) is the second storage probe of a normal boot. It runs after the SPI path fails. It uses the NAND Flash (NF) sequencer hardware at 0x2002A000.

## Hardware Initialization

`nf_boot_hw_init()` prepares the NF controller and the L2 buffer:

1. SYSCTRL+0x74: clear bits [4:3], set bit 3. This selects the NF sharepin function.
2. SYSCTRL+0x78: set bits [23:22, 18:16, 9] (0x00C70200). This enables the NF clock and I/O paths.
3. L2CTR_ASSIGN_REG1 (0x2002C090): clear bits [11:9]. This unbinds any earlier L2 buffer assignment on the NF path.
4. L2CTR_BUF0_7_CFG (0x2002C088): set bit 16 to enable, then set bit 24 to flush the common buffer.
5. L2CTR_DMA_PATH_CFG (0x2002C084): set bits [29:28].
6. NF timing register 0 (0x2002A05C): write the default value 1006545 (0x0F5BD1).

## Probe Procedure

1. Step through the 8 probe parameter sets (`nf_probe_params[0..7]`). The name says probe, but these parameters configure the NF sequencer for different flash types and modes.

2. For each parameter set:
   1. Delay 10 ticks.
   2. Copy the parameter structure, 5 words: counts, command layout, timing config 0 and 1, pre-delay ticks with sequencer delay ticks.
   3. Issue the probe command sequence with `nf_issue_probe_sequence()`.
   4. Read 0x20 bytes from the NF data buffer into L2BUF_01.
   5. Look for the `"ANYKA382"` signature at L2BUF_01 offset +0x04.

3. On a signature match:
   1. Copy 5 dwords from offset +0x0C of the L2 data into `nf_tail`.
   2. Validate `chunks_per_page`, which must be 1, 4, or 8. On an invalid value the ROM returns 0 immediately. It does **not** continue to the next parameter set.
   3. If the load descriptor in the header carries non-zero timing overrides, apply them with `nf_set_boot_timings()`.
   4. Delay for `pre_delay_ticks` from the header.
   5. Issue the probe sequence again and read 0x200 bytes, the full first page.
   6. Copy 0x46 dwords, 280 bytes, from offset +0x0C for the complete header tail structure.
   7. Delay for `pre_delay_ticks` again.
   8. Dispatch by `image_type`.

4. If none of the 8 parameter sets finds a valid image, return 0.

## NF Sequencer Command Execution

`nf_issue_probe_sequence(param, page_addr)` programs the NF sequencer FIFO:

1. Clear NF_SEQ_CTRL_STA.
2. Write WORD0 as `(cmd1 << 11) | 0x64` to output the first command byte.
3. Write the prefix and dummy words: `0x62`, repeated `seq_prefix_count` times.
4. Write the address bytes. For each of the `addr_byte_count` bytes, encode `((page_addr >> (8*i)) << 11) | 0x62`.
5. If `cmd_count > 1`, write a second command byte as `(cmd2 << 11) | 0x64`.
6. Write the wait word. With a non-zero delay tick count, write `(ticks << 11) | 0x401`. Otherwise write the default 21505, which is `(10 << 11) | 0x401`, a 10-tick wait.
7. Set NF_SEQ_CTRL_STA = 0x40000600 to start the sequence.
8. Poll bit 31 of NF_SEQ_CTRL_STA until the sequence completes.

[memory-map.md](memory-map.md) decodes the per-word bit fields behind these constants.

## NF Data Read

`nf_read_chunk_to_buf(dst, byte_count)`:

1. Rejects a read larger than 0x200 bytes.
2. Programs the NF DMA control register (0x2002B000) with the byte count and the buffer configuration: `(byte_count << 7) | 0x100018`.
3. Programs the sequencer: WORD0 = `((byte_count - 1) << 11) | 0x119`, the read-data micro-op.
4. Starts the sequence with NF_SEQ_CTRL_STA = 0x40000600.
5. Waits for sequencer done, bit 31.
6. Waits for DMA done: polls 0x2002B000 bit 6, then writes bit 6 to clear it.
7. Copies `byte_count / 4` words from the L2BUF_00 base (0x48000000) to `dst`, then flushes with bit 24 of L2CTR_BUF0_7_CFG (0x2002C088).

## Payload Loading

`nf_load_payload(dst, param, start_page)`:

1. For each page from `start_page` to `start_page + page_count - 1`:
   1. Issue the read sequence for that page address.
   2. For each chunk in the page, `chunks_per_page` times, read 0x200 bytes into the next position in `dst`.
2. Each chunk advances the destination pointer by 128 words, 512 bytes.

## Probe Parameter Structure

Each of the 8 probe parameter sets (`nf_probe_param_t`) holds:

| Field | Size | Description |
| --- | --- | --- |
| counts | 4B | Packed: byte0 = chunks_per_page, byte1 = page_count, byte2 = cmd_count, byte3 = seq_prefix_count |
| command | 4B | addr_byte_count (byte), cmd1 (byte), cmd2 (byte), padding |
| timing_cfg0 | 4B | NF timing register 0 override (0 = keep default) |
| timing_cfg1 | 4B | NF timing register 1 override (0 = keep default) |
| delay_pair | 4B | Low 16 bits = pre_delay_ticks before the bulk reads. High 16 bits = sequencer wait ticks. |

The 8 parameter sets cover different NAND flash configurations. The ROM table at `nf_probe_params` decodes to:

- 4 address bytes, command `0x00`
- 4 address bytes, commands `0x00` then `0x30`
- 3 address bytes, command `0x00`
- 3 address bytes, commands `0x00` then `0x30`
- 2 address bytes, command `0x00`
- 2 address bytes, commands `0x00` then `0x30`
- 5 address bytes, command `0x00`
- 5 address bytes, commands `0x00` then `0x30`

All eight entries use `chunks_per_page=1`, `page_count=1`, `seq_prefix_count=1`, `timing_cfg0=0x000C3671`, `timing_cfg1=0x000D3637`, and a `delay_pair` of `0x000A000A`, 10 ticks in both fields.

## Timing Override

`nf_set_boot_timings(cfg0, cfg1)` writes cfg0 to 0x2002A05C when cfg0 is non-zero, and cfg1 to 0x2002A060 when cfg1 is non-zero. The boot image header can therefore override the default NF timing for a slower or a faster flash device. [memory-map.md](memory-map.md) gives the field layout of both values.

## Image Type Dispatch

| image_type | Action |
| --- | --- |
| 6 | Run `apply_reg_init_script()`, load the payload to DDR (0x30000000), return 2 |
| 8 | Load the payload to L2BUF_01 (0x48000200), return 1 |
| other | Continue the probe. A valid image does not reach this case. |

## Return Values

| Value | Meaning                                                      |
| ----- | ------------------------------------------------------------ |
| 0     | No valid NAND flash image found                              |
| 1     | Type-8 image loaded to L2BUF_01 (0x48000200)                 |
| 2     | Type-6 image loaded to DDR (0x30000000), init script applied |
