# sd-speed

Times the SD read paths of `lib/sd.c` against native multi-block reads, L2 DMA and 31 MHz high speed. The probe runs with the MMU and the caches off, the state in which openNBOOT reads `BOOT.BIN`, and repeats each phase with the I-cache on.

The probe compiles `lib/sd.c` and `lib/fat.c` into itself, so the baseline phases run the openNBOOT code without change.

## Phases

Each phase starts with `sd_init()` and one CMD17 of LBA 0. The first transfer after initialization always fails once, see [sdhc-driver.md](../../../docs/nk/sdhc-driver.md#first-transfer-after-initialization). That read absorbs the failure and its time goes in `first_us`.

| Phase | Path |
| --- | --- |
| `file-cmd17` | `fat_load_file()`, as openNBOOT calls it |
| `raw-cmd17` | `sd_read_block()` once for each sector |
| `raw-pio18-64` | CMD18 of 64 sectors, CPU copy from L2 common buffer 4, CMD12 |
| `raw-dma-8`, `raw-dma-64`, `raw-dma-1024` | CMD23 and CMD18 of 8, 64 or 1024 sectors, L2 DMA in 8 KiB chunks through common buffer 2 |
| `file-dma` | The cluster chain from the `fat.c` helpers, contiguous clusters merged into requests of up to 1024 sectors, read as in `raw-dma-1024` |
| `hs-*` | CMD6 high speed and `MCI_CLOCK` divider `0x0101` (31 MHz) before the work |

The raw phases read `raw_sectors` sectors from the start of the first FAT partition. The `file-*` phases read `BOOT.BIN` from the root directory. Every phase except the first `file-cmd17` and the first `raw-cmd17` compares its data with theirs. The probe writes nothing to the card.

## Running

Start [gdbstub](../../gdbstub/) on the device, then from this directory:

```
make
arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem00011' -x run.gdb
```

To read another root file or a different range, or to skip phases, add a `set var` line between `load` and `jump` in `run.gdb`, for example `set var params.raw_sectors = 8192`, `set var params.file = "AIPCBOOTBIN"`, or `set var params.skip = 4`. Bit n of `skip` skips the phase in row n of the phase table, counted from 0.

The probe masks IRQ and FIQ, so the debugger cannot interrupt it. A run takes about one minute.

## Output

One line for each phase and I-cache state:

| Column | Meaning |
| --- | --- |
| `ic` | 1 when the I-cache is on |
| `rc` | 0, or the error code of the step that failed |
| `init_us` | `sd_init()` |
| `first_us` | The first CMD17 after `sd_init()`, with its retry |
| `single_us` | One more CMD17 |
| `hs_us` | CMD6, the clock change and CMD13 |
| `work_us`, `kB/s` | The phase itself |
| `mismatch` | `ffffffff`, or the first byte offset that differs from the reference |
| `got`, `want`, `nbad` | At a mismatch: the word read, the reference word, and the number of bytes that differ |
| `clock` | `MCI_CLOCK` after the phase |

## Results

v1.58.2 device, 2026-10-09. gdbstub ran at `0x33A00000`. `BOOT.BIN` on the card was aipc-boot, 187496 bytes in 6 contiguous runs of clusters. The partition has 512-byte clusters. Three runs gave times that agree within 1 percent.

With `raw-pio18-64` skipped (`params.skip = 4`):

```
c1=0x00050078 raw_lba=2048 raw_sectors=2048 file_size=187496 file_runs=6
phase            ic  rc     init_us first_us single_us   hs_us    work_us    bytes   kB/s mismatch
file-cmd17        0   0       61960     1427       445       0      99841   187496   1833 ffffffff
file-cmd17        1   0       23413     1432       256       0      97623   187496   1875 ffffffff
raw-cmd17         0   0       61955     1435       439       0     615059  1048576   1664 ffffffff
raw-cmd17         1   0       23426     1426       252       0     614997  1048576   1665 ffffffff
raw-dma-8         0   0       61958     1426       438       0     168867  1048576   6063 ffffffff
raw-dma-8         1   0       23415     1417       256       0     169006  1048576   6058 ffffffff
raw-dma-64        0   0       61960     1443       444       0     113080  1048576   9055 ffffffff
raw-dma-64        1   0       23413     1418       256       0     113084  1048576   9055 ffffffff
raw-dma-1024      0   0       61965     1418       434       0     105155  1048576   9737 ffffffff
raw-dma-1024      1   0       23424     1429       256       0     105141  1048576   9739 ffffffff
file-dma          0   0       61957     1423       438       0      26377   187496   6941 ffffffff
file-dma          1   0       23421     1429       257       0      24067   187496   7607 ffffffff
hs-raw-cmd17      0   0       61963     1427       434    5187     581972  1048576   1759 ffffffff
hs-raw-cmd17      1   0       23424     1442       446    3060     582491  1048576   1757 ffffffff
hs-raw-dma-1024   0   0       61956     1460       439    5216      72792  1048576  14067 ffffffff
hs-raw-dma-1024   1   0       23424     1420       448    3064      72808  1048576  14064 ffffffff
hs-file-dma       0   0       61961     1430       439    5221      20802   187496   8801 ffffffff
hs-file-dma       1   0       23421     1417       445    3060      18505   187496   9894 ffffffff
```

- openNBOOT reads `BOOT.BIN` in about 0.16 s: 62 ms for `sd_init()`, 1.4 ms for the first read, and 100 ms for the file.
- One CMD17 for each sector gives 1.7 MB/s. The 31 MHz clock and the I-cache do not change this rate, so the limit is the time that the card takes for each command.
- CMD23 and CMD18 with L2 DMA give 9.7 MB/s at 20.67 MHz and 14.1 MB/s at 31 MHz with 1024-sector requests. 8-sector requests give 6.1 MB/s.
- The I-cache shortens `sd_init()` from 62 ms to 23 ms, because its delays are counted in loop passes. It does not change the transfers.
- The first read after `sd_init()` fails once and costs about 1 ms with its retry.

With `raw-pio18-64` included, that phase fails with `rc=-21` in both I-cache states: `MCI_DATAEND` does not come within 2 s. The next phase, `raw-dma-8` with the I-cache off, then reads wrong data from byte 0 (`e0000001` in place of `6d9058eb`), although `sd_init()` runs between the two phases. All other phases match. Thus the first DMA read after an aborted multi-block PIO read is not reliable.
