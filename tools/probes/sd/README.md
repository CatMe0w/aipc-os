# AK7802 SD/MMC pre-validation

This suite checks AK7802 MCI behavior before it is used by the Linux MCI host driver. Each stub isolates one controller assumption and returns a nonzero status when its acceptance conditions are not met. Confirmed register behavior and Linux results are recorded in [SDHC Driver](../../../docs/nk/sdhc-driver.md); this file only describes the probe suite and its safety boundary.

## Running

Build the stubs and invoke the runner from the repository root:

```
make -C tools/probes/sd/stub
uv run tools/probes/sd/run_probe.py PROBE.bin --words N [options]
```

Use `--words 32` for `sd_irq_window`, 64 for `sd_baseline` and `sd_irq`, 80 for `sd_highspeed`, 152 for `sd_init`, and 160 for the remaining probes. Read probes use `--lba`; destructive probes require `--write-lba`. Other source-specific options are `--target-offset`, `--four-bit`, `--capture-only`, and `--verify-only`.

Initialize DDR before running the multiblock write, high-speed, IRQ, or DDR-linked variants. Most other stubs execute from the default L2 address. A DDR-linked binary also requires the `--stub-addr` selected by its linker target: `0x30020000`, `0x30030000`, `0x30040000`, `0x30050000`, `0x30090000`, or `0x30110000`. The Makefile is the authoritative mapping from binary name to linker script.

Some probes consume state left by another stub. Multiblock, write, and high-speed comparisons begin with `sd_pio_withdata` for the same LBA. `sd_irq` and `sd_irq_window` consume the state left by `sd_highspeed_41mhz`. Do not reset between members of one chain; power-cycle before an independent run.

Every result starts with magic `0x53445052`, a format version, an experiment number, a status, and a payload length. The runner rejects an invalid header and exits unsuccessfully when status is nonzero.

## Write safety

Never reuse an LBA merely because it was unallocated on a previous card. Confirm the entire range is outside every partition on the installed card before passing `--write-lba`. A failed probe may stop after writing its test pattern and before restoring the original data.

The multiblock write source supports a read-only `--capture-only` pass and stores the original range at DDR `0x30010000`. Save that range before the destructive pass. After restoration, power-cycle the device, upload the saved reference to the same address, and run `--verify-only`. The reference length is the request size. The single-block `sd_write` probe instead consumes the sector retained by the preceding PIO read and must also be checked again after a power cycle.

## Probe sources

- `sd_baseline.c` captures untouched MCI, L2, clock, and pin state. `sd_init.c` reproduces Linux SD enumeration.
- `sd_pio.c` builds the two inner-FIFO CMD17 encodings and the single-block L2 PIO variant. `sd_write.c` tests CMD24 ordering and restoration.
- `sd_multiread.c` and `sd_multiread8.c` test native CMD18, block-end behavior, CMD12, and one-bit versus four-bit operation.
- `sd_multiwrite8.c` is the common source for CMD25, common-buffer PIO, L2 DMA, CMD23, request-size, chunk-size, high-speed, and scatter-gather variants.
- `sd_highspeed.c` checks CMD6 selection and compares CMD17 data after changing the clock divider.
- `sd_irq.c` observes command, data, and L2 DMA completion through the main interrupt controller. `sd_irq_window.c` tests whether merely enabling an idle source asserts an interrupt.

`sd_pio.c` varies `PIO_WITHDATA` and `PIO_L2`. `sd_multiwrite8.c` varies the boolean `USE_L2`, `USE_L2_DMA`, `USE_CMD23`, `USE_HIGH_SPEED`, and `USE_SCATTER_DMA` switches. Built request sizes are 8, 64, 128, 256, 512, and 1024 blocks. DMA chunks are 4096, 8192, or 16384 bytes. High-speed divider values are `0x0101` for 31 MHz and `0x0001` for 41.33 MHz. The scatter-gather variant uses 4096-byte data segments at an 8192-byte stride. Exact combinations and DDR placements remain in the Makefile rather than being duplicated here.

## Confirmed boundaries

Cold boot leaves the MCI idle except for `MCI_FIFOEMPTY` and does not configure an L2 buffer for it. SD enumeration tolerates the expected CMD5 timeout without resetting the controller. Long responses map directly from `MCI_RESPONSE0..3` to Linux `resp[0..3]`.

Both CMD17 command encodings return the same sector. Inner-FIFO reads require `MCI_FIFOFULL | MCI_RXACTIVE`; writes issue CMD24 before enabling the data path and require `MCI_FIFOEMPTY | MCI_TXACTIVE`. The single-block and multiblock write probes restored their original data and passed an independent power-cycle check.

Native CMD18 and CMD25 work through the inner FIFO and common buffer 2. L2 DMA works in both directions with 64-byte operations and repeated chunks while one MCI request remains active. An 8 KiB chunk passes; a 16 KiB chunk stalls after the first sector. CMD23 bounds successful CMD18 and CMD25 requests without CMD12, while an aborted request still requires CMD12.

Requests through 512 KiB passed with 8 KiB chunks. The scatter-gather probe crossed discontiguous 4 KiB segments without modifying the guard gaps. CMD6 high-speed selection passed at 31 MHz and at the retained 41.33 MHz divider setting.

MCI completion reaches main interrupt source 22 and common-buffer 2 DMA completion reaches source 10. An idle L2 buffer asserts source 10 as soon as `L2_BUFINTEN[11]` is enabled, so software must set `L2_DMAREQ[26]` first and exclude local IRQ delivery across the two writes.

The tested scope does not include requests above 512 KiB, frequencies above 41.33 MHz, hot removal, or deliberate data-error recovery.
