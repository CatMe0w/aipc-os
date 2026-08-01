# AK7802 SD/MMC pre-validation

This suite checks which MCI behaviors from successor Linux 2.6 drivers also apply to AK7802 before they are used in a Linux 7.0 host driver. Each probe tests one narrow part of the controller and reports a nonzero status when its acceptance conditions are not met.

## Running the probes

Build the stubs from the repository root:

```
make -C tools/probes/sd/stub
```

Power-cycle the target into USB boot before each independent run, then execute one stub:

```
uv run tools/probes/sd/run_probe.py tools/probes/sd/stub/sd_init.bin --words 152
```

The inner-FIFO and L2 read probes accept `--lba N`; without it they read LBA 0. The write probe requires the explicitly destructive `--write-lba N` option and refuses to run without it. The selected LBA must first be verified as unallocated.

The write experiment is a two-stage exception to the cold-start rule. First use the inner-FIFO read probe to initialize the card and retain the selected sector in L2 SRAM. Then run the write probe without resetting or power-cycling between the two commands:

```
uv run tools/probes/sd/run_probe.py tools/probes/sd/stub/sd_pio_withdata.bin --words 160 --lba N
uv run tools/probes/sd/run_probe.py tools/probes/sd/stub/sd_write.bin --words 160 --write-lba N
```

After the write probe restores the sector, power-cycle the target and read the same LBA again. Its 128 data words must match the first read.

The available experiments are:

| Stub | Result words | Purpose |
| --- | ---: | --- |
| `sd_baseline.bin` | 64 | Capture untouched MCI and L2 state |
| `sd_init.bin` | 152 | Run the Linux MMC enumeration sequence |
| `sd_pio_nodata.bin` | 160 | Read one block without `CPSM_WITHDATA` |
| `sd_pio_withdata.bin` | 160 | Read one block with `CPSM_WITHDATA` |
| `sd_l2.bin` | 160 | Read one block through an L2 common buffer |
| `sd_write.bin` | 160 | Write and restore one explicitly selected block through the inner FIFO |

Every result begins with the magic value `0x53445052`, a format version, an experiment number, a status, and a payload length. The runner exits unsuccessfully when the header is invalid or the experiment status is nonzero.

## Results

All five read-only experiments completed successfully and were repeated from independent cold starts with the same controller behavior and block contents. The write experiment also completed successfully on an unallocated sector and restored its original contents.

### Cold-boot state

The primary MCI was quiescent except for `MCI_FIFOEMPTY`. The L2 controller was visible but had no common buffer configured for MCI. A Linux driver must initialize any L2 buffer it intends to use.

### Card enumeration

The enumeration probe reproduced the non-SPI Linux MMC sequence, including the expected CMD5 timeout while probing for SDIO. CMD55 and ACMD41 completed immediately afterward without resetting the controller. The card then completed CMD2, CMD3, CMD9, CMD7, and CMD13 and entered transfer state.

Long response registers are already in Linux `mmc_command.resp[]` order:

```
resp[0] = MCI_RESPONSE0
resp[1] = MCI_RESPONSE1
resp[2] = MCI_RESPONSE2
resp[3] = MCI_RESPONSE3
```

Reversing these words corrupts CID and CSD decoding. The reverse byte-copy order used by the target WinCE interface is an API layout detail, not the Linux representation.

### Inner-FIFO reads

Both CMD17 encodings completed a 512-byte read:

| Command value | Meaning | Result |
| ---: | --- | --- |
| `0x000000A3` | No `CPSM_WITHDATA` | Success |
| `0x000008A3` | `CPSM_WITHDATA` set | Success |

Before every FIFO access, the probe required both `MCI_FIFOFULL` and `MCI_RXACTIVE`. It transferred exactly 128 words and required both `MCI_DATAEND` and `MCI_DATABLOCKEND` without a data error. The two command variants returned identical data, so `CPSM_WITHDATA` is not the cause of the unusable Linux path.

### L2 common-buffer read

The L2 probe routed MCI to common buffer 6 and used `MCI_DMACTRL=0x01000001`. The buffer count progressed from 0 to 8 during the transfer and returned to 0 after the CPU copied 512 bytes. Its contents matched both inner-FIFO reads.

Buffer 6 was chosen because the probe image overlaps buffer 2, which the successor driver uses. The result validates the common-buffer model but does not prove that buffer 2 is available to Linux.

### Inner-FIFO write

The write probe follows the ordering shared by the successor Linux drivers and the target WinCE driver. It sends CMD24 first, enables the transmit data path only after the command response, and writes each word only while both `MCI_FIFOEMPTY` and `MCI_TXACTIVE` are asserted. It consumes the sector retained by the preceding read probe, writes a deterministic test pattern, verifies the pattern with CMD17, restores the original sector, and verifies the restoration. A failed run can leave the test pattern in the selected sector, which is why only a confirmed unallocated LBA may be used.

The test used LBA 8388608, beyond the last partition ending at LBA 6555647. Pattern readback and original-data readback both passed. After a power cycle, an independent read of the same LBA matched the pre-test sector, confirming that restoration reached the card rather than remaining only in controller state. CMD13 returned `0x00000900` after each write, indicating transfer state with the card ready for data.

## Linux driver implications

- Copy long responses from `MCI_RESPONSE0..3` directly into `resp[0..3]`.
- Treat `MCI_FIFOFULL | MCI_RXACTIVE` as a conjunctive read-ready condition.
- Start the write data path only after CMD24 succeeds, then require `MCI_FIFOEMPTY | MCI_TXACTIVE` before each FIFO word.
- Poll CMD13 until the card is ready after a completed write.
- Do not reset the controller solely because CMD5 times out during normal SD discovery.
- Either CMD17 command encoding is accepted by the tested controller path.
- Initialize and allocate an L2 common buffer before enabling the MCI L2 path.

The recorded hardware results do not cover interrupts, clock changes, four-bit writes, multi-block requests, hot removal, deliberate error recovery, or Linux block-layer integration.
