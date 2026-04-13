# NAND Flash Boot Path

The NAND flash boot path (`probe_flash_boot_source`) is the second storage
probe attempted during normal boot, after the SPI path fails. It uses the
NAND Flash (NF) sequencer hardware at 0x2002A000.

## Hardware Initialization

`nf_boot_hw_init()` prepares the NF controller and L2 buffer:

1. SYSCTRL+0x74: clear bits [4:3], set bit 3 - selects the NF sharepin
   function.
2. SYSCTRL+0x78: set bits [23:22, 18:16, 9] (0x00C70200) - enables NF-related
   clock and I/O paths.
3. L2CTR_ASSIGN_REG1 (0x2002C090): clear bits [11:9] - unbind any
   previous L2 buffer assignment for the NF path.
4. L2CTR_BUF0_7_CFG (0x2002C088): set bit 16 (enable), then set bit 24
   (flush/reset the common buffer).
5. L2CTR_DMA_PATH_CFG (0x2002C084): set bits [29:28].
6. NF timing register 0 (0x2002A05C): write default value 1006545
   (0x0F5BD1).

## Probe Procedure

1. Iterate through 8 probe parameter sets (`nf_probe_params[0..7]`). Despite
   the name, these parameters configure the NF sequencer for probing different
   flash types/modes.

2. For each parameter set:
   a. Delay 10 ticks.
   b. Copy the parameter structure (5 words: counts, command layout, timing
   config 0/1, pre-delay ticks + sequencer delay ticks).
   c. Issue the probe command sequence via `nf_issue_probe_sequence()`.
   d. Read 0x20 bytes from the NF data buffer into L2BUF_01.
   e. Check for the `"ANYKA382"` signature at L2BUF_01 offset +0x04.

3. On signature match:
   a. Copy 5 dwords from offset +0x0C of the L2 data into `nf_tail`.
   b. Validate `chunks_per_page` (must be 1, 4, or 8).
   c. If the header load descriptor includes non-zero timing overrides,
   apply them via `nf_set_boot_timings()`.
   d. Delay for `pre_delay_ticks` from the header.
   e. Re-issue the probe sequence and read 0x200 bytes (full first page).
   f. Copy 0x46 dwords (280 bytes) from offset +0x0C for the complete
   header tail structure.
   g. Delay again for `pre_delay_ticks`.
   h. Dispatch by `image_type`.

4. If no valid image is found across all 8 parameter sets, return 0.

## NF Sequencer Command Execution

`nf_issue_probe_sequence(param, page_addr)` programs the NF sequencer FIFO:

1. Clear NF_SEQ_CTRL_STA.
2. Write WORD0: `(cmd1 << 11) | 0x64` - output the first command byte.
3. Write prefix/dummy words: `0x62` repeated `seq_prefix_count` times.
4. Write address bytes: for each of `addr_byte_count` bytes, encode
   `((page_addr >> (8*i)) << 11) | 0x62`.
5. If `cmd_count > 1`, write a second command byte:
   `(cmd2 << 11) | 0x64`.
6. Write the wait/delay word: if a non-zero delay tick count is specified,
   `(ticks << 11) | 0x401`; otherwise use the default 21505
   (= `(10 << 11) | 0x401`, i.e., 10-tick wait).
7. Set NF_SEQ_CTRL_STA = 0x40000600 to launch the sequence.
8. Poll bit 31 of NF_SEQ_CTRL_STA until the sequence completes.

## NF Data Read

`nf_read_chunk_to_buf(dst, byte_count)`:

1. Rejects reads larger than 0x200 bytes.
2. Programs the NF DMA control register (0x2002B000) with the byte count
   and buffer configuration: `(byte_count << 7) | 0x100018`.
3. Programs the sequencer: WORD0 = `((byte_count - 1) << 11) | 0x119`
   (read data micro-op).
4. Launches with NF_SEQ_CTRL_STA = 0x40000600.
5. Waits for sequencer done (bit 31).
6. Waits for DMA done: polls 0x2002B000 bit 6, then writes bit 6 to clear.
7. Copies `byte_count / 4` words from L2BUF_00 base (0x48000000) to `dst`,
   then flushes by setting L2CTR_BUF0_7_CFG (0x2002C088) bit 24.

## Payload Loading

`nf_load_payload(dst, param, start_page)`:

1. For each page from `start_page` to `start_page + page_count - 1`:
   a. Issue the probe/read sequence for that page address.
   b. For each chunk within the page (`chunks_per_page` iterations):
   read 0x200 bytes into sequential positions in `dst`.
2. Each chunk advances the destination pointer by 128 words (512 bytes).

## Probe Parameter Structure

Each of the 8 probe parameter sets (`nf_probe_param_t`) contains:

| Field           | Size | Description                                                                                     |
| --------------- | ---- | ----------------------------------------------------------------------------------------------- |
| counts          | 4B   | Packed: byte0 = chunks_per_page, byte1 = page_count, byte2 = cmd_count, byte3 = seq_prefix_count |
| command         | 4B   | addr_byte_count (byte), cmd1 (byte), cmd2 (byte), padding                                       |
| timing_cfg0     | 4B   | NF timing register 0 override (0 = keep default)                                            |
| timing_cfg1     | 4B   | NF timing register 1 override (0 = keep default)                                            |
| delay_pair      | 4B   | Low 16 bits = pre_delay_ticks before bulk reads; high 16 bits = sequencer wait ticks         |

The 8 parameter sets cover different NAND flash configurations. The ROM table
at `nf_probe_params` currently decodes to:

- 4 address bytes, command `0x00`
- 4 address bytes, commands `0x00` then `0x30`
- 3 address bytes, command `0x00`
- 3 address bytes, commands `0x00` then `0x30`
- 2 address bytes, command `0x00`
- 2 address bytes, commands `0x00` then `0x30`
- 5 address bytes, command `0x00`
- 5 address bytes, commands `0x00` then `0x30`

All eight entries use `chunks_per_page=1`, `page_count=1`,
`seq_prefix_count=1`, `timing_cfg0=0x000C3671`, `timing_cfg1=0x000D3637`,
and a `delay_pair` of `0x000A000A` (10 ticks for both fields).

## Timing Override

`nf_set_boot_timings(cfg0, cfg1)`: if cfg0 is non-zero, writes it to
0x2002A05C; if cfg1 is non-zero, writes it to 0x2002A060. This allows
the boot image header to override the default NF timing for slower or
faster flash devices.

## Image Type Dispatch

| image_type | Action                                                                    |
| ---------- | ------------------------------------------------------------------------- |
| 6          | Run `apply_reg_init_script()`, load payload to DDR (0x30000000), return 2 |
| 8          | Load payload to L2BUF_01 (0x48000200), return 1                           |
| other      | Continue probing (should not occur with valid images)                     |

## Return Values

| Value | Meaning                                                      |
| ----- | ------------------------------------------------------------ |
| 0     | No valid NAND flash image found                              |
| 1     | Type-8 image loaded to L2BUF_01 (0x48000200)                 |
| 2     | Type-6 image loaded to DDR (0x30000000), init script applied |
