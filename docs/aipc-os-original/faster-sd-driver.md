# Faster SD/MMC Driver

The Linux MMC host driver for the AK7802 MCI controller. [docs/nk/sdhc-driver.md](../nk/sdhc-driver.md) records what the controller does and what the WinCE driver does with it. This document records what we chose, and what it reaches.

## Request Size and Clock

The hardware fixes the chunk size. A single L2 DMA operation list cannot exceed 8 KiB, thus every request splits into 8 KiB chunks, and one CMD18 or CMD25 stays open across them. What we could choose was the request size above that, and the bus clock.

Both were swept on the same card, first at 20.67 MHz and then with SD high-speed mode at 31 MHz:

| Request limit | Clock | Read | Write |
| --- | --- | --- | --- |
| 4 KiB | 20.67 MHz | 3.0 MB/s | 0.4 to 0.5 MB/s |
| 32 KiB | 20.67 MHz | 5.2 MB/s | 3 to 4 MB/s |
| 64 KiB | 20.67 MHz | 5.4 MB/s | 3.6 to 4.4 MB/s |
| 128 KiB | 20.67 MHz | 5.8 MB/s | 3.8 to 4.7 MB/s |
| 128 KiB | 31 MHz | 6.6 MB/s | 4.7 to 5.6 MB/s |
| 256 KiB | 31 MHz | 7.3 MB/s | 4.8 to 5.7 MB/s |
| 512 KiB | 31 MHz | 7.8 MB/s | 4.6 to 5.9 MB/s |

The sweep stopped at 512 KiB. Above it the gain no longer justified a larger coherent buffer.

Removing that buffer helped more than enlarging it. A direct DMA mapping of the Linux scatterlist, in place of the coherent bounce buffer, raised reads to about 9.0 MB/s and writes to a typical 6.3 MB/s, in a range of 5.5 to 6.7 MB/s. The L2 operation list can point at discontiguous DDR segments, which is what makes the scatterlist mapping possible at all.

## Asymmetric Clock Divider

`MCI_CLOCK` holds two dividers, one for the low half of the bus clock and one for the high half. Our driver first wrote the same value into both, which is the obvious reading of the register. That choice costs a usable frequency. From the 124 MHz input, equal halves give 31 MHz at `0x00190101` and then 62 MHz at `0x00190000`. 62 MHz is above the 50 MHz ceiling of SD high speed, thus 31 MHz was the top of the range. `drivers/mmc/host/anyka-mmc.c` carried that restriction.

The WinCE driver does not restrict itself that way. Its split is `low = divider - (divider >> 1)` and `high = divider >> 1`, which gives unequal halves for every odd divider. See [docs/nk/sdhc-driver.md](../nk/sdhc-driver.md). Divider 1 gives `low = 1` and `high = 0`, that is `MCI_CLOCK = 0x00190001`, and one more step of bus clock:

```
short half = 1 / 124 MHz =  8.06 ns
long half  = 2 / 124 MHz = 16.13 ns
period     = 3 / 124 MHz = 24.19 ns
frequency  = 41.33 MHz
```

Linux runs on it. systemd boots, ext4 reads and writes work, and the rate measures about 10.7 MB/s for reads and 7.0 MB/s for writes.

### The 33/67 Duty Cycle Is Not a Violation

The waveform is 33 percent low and 67 percent high, which looks wrong next to the near-square clock of a symmetric divider. It is not a violation. The 3.3 V legacy High Speed mode of the SD Physical Layer Simplified Specification v9.10 sets three limits on page 282: at most 50 MHz, `tWL` and `tWH` of at least 7 ns each, and a rise or fall time of at most 3 ns. The mode bounds the absolute width of each half, not the ratio between them. A 45/55 style ratio requirement belongs to the DDR and UHS modes, or to the output guarantee of one specific host controller. We run 3.3 V High Speed, which CMD6 function 1 selects.

Both halves clear the limit. 8.06 ns and 16.13 ns are both above 7 ns, and 41.33 MHz is below 50 MHz.

The margin on the short half is 1.06 ns. Register arithmetic does not prove that the card socket still sees 8.06 ns. Pad delay, asymmetric edges and signal integrity can each take part of it. Two conclusions follow. On this board the probe completes its reads, writes, restores and power-cycle checks, thus experimental Linux acceptance at 41.33 MHz is reasonable. For a driver that must work with any card, do not claim adequate margin until a scope confirms `tWL` and `tWH` of at least 7 ns at the socket. 31 MHz stays our conservative default, and 41.33 MHz stays experimental.

## Interrupt-Driven Completion

The first interrupt-driven implementation enabled `L2_BUFINTEN[11]` before it set `L2_DMAREQ[26]`. An idle common buffer asserts its completion source the moment the enable goes on, thus an interrupt could arrive between the two writes, and the driver accepted it as completion of a request that had not started. The result was DMA timeouts, incorrect rootfs data, and persistent ext4 metadata corruption.

The corrected implementation sets the buffer request first, enables the completion interrupt second, and excludes local IRQ delivery across the two writes. Any driver on this controller needs the same order.

The corrected kernel boots systemd from SD. Over a 64 MiB raw read the MCI interrupt count rises by 469 and the L2 DMA interrupt count by 8566, with no interrupt errors. That read needs 8192 L2 completions at the 8 KiB chunk size, and the rest is background rootfs traffic between the two counter snapshots. The transfer runs at 10.1 MB/s, but on a different card from the sweep above, thus the two numbers do not compare.

## Not Covered

Interrupt-driven write completion, requests larger than 512 KiB, and hot removal have no test yet. Neither does a scope measurement of `tWL` and `tWH` at the card socket at 41.33 MHz.
