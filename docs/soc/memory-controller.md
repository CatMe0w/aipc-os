# Memory controller

The DDR controller of the AK7802 is at physical `0x2002D000`. It has three registers. The ASIC clock drives it, thus any change to the ASIC clock is a change to the memory clock.

## Address decoding

The block decodes only the low five address bits. `0x2002D000`, `0x2002D020` and `0x2002D040` return the same three words. Only `+0x00`, `+0x04` and `+0x08` hold data. `+0x0C`, `+0x10` and `+0x14` read `0xFFFFFFFF`, and `+0x18` and `+0x1C` read zero.

`+0x0C` is not a fourth register of this block. The AK88 RAM controller names the same offset a DMA priority configuration register.

## Command register, +0x00

The register drives the DRAM command pins directly. Software builds one command in the field bits, then sets the request bit to issue it.

| Bits | Name | Meaning |
| --- | --- | --- |
| 13:0 | ADDR | Address pins for the command |
| 14 | BANK0 | Bank select 0 |
| 15 | BANK1 | Bank select 1 |
| 16 | WE | Write enable pin level |
| 17 | CAS | Column address strobe pin level |
| 18 | RAS | Row address strobe pin level |
| 19 | CS | Chip select pin level |
| 20 | REQ | Command request. Write 1 to issue. Self clearing. |
| 28:21 | COUNT | Command repeat count |
| 29 | VALID | Normal operation. Auto refresh runs while this is 1. |
| 30 | CKE | Clock enable pin level |
| 31 | CL | CAS latency select |

The request bit is the only self clearing bit. A live read after the boot script writes `0x60170000` returns `0x60070000`, which is the same word with bit 20 clear. Every other bit reads back as written.

A level of 1 means the pin is driven high. RAS, CAS and CS are active low on the DRAM, thus 1 means not asserted.

### Command words in use

| Word | CKE | VALID | RAS | CAS | WE | Meaning |
| --- | --- | --- | --- | --- | --- | --- |
| `0x60170000` | 1 | 1 | 1 | 1 | 1 | NOP, normal operation on |
| `0x40170000` | 1 | 0 | 1 | 1 | 1 | NOP, normal operation off |
| `0x40110000` | 1 | 0 | 0 | 0 | 1 | Auto refresh |
| `0x00110000` | 0 | 0 | 0 | 0 | 1 | Self refresh entry |

`0x00110000` is the auto refresh command with CKE taken low in the same write, which is the JEDEC self refresh entry.

## Timing register, +0x04

The timing fields count memory clocks. The Meaning column converts them at the 124 MHz boot clock.

| Bits | Name | Value on this board | Meaning |
| --- | --- | --- | --- |
| 2:0 | Capacity | 5 | 64 MB, that is `1 << (n + 1)` |
| 5:3 | tRP | 2 | 16.1 ns |
| 9:6 | tRFC | 14 | 112.9 ns |
| 12:10 | tRCD | 2 | 16.1 ns |
| 15:13 | tWR | 3 | 24.2 ns |
| 18:16 | - | 0 | |
| 22:19 | tRAS | 10 | 80.6 ns |
| 24:23 | tWTR | 2 | 16.1 ns |
| 25 | AHB count | 1 | |
| 26 | DDR | 1 | 0 selects SDR SDRAM |
| 27 | MCLK enable | 1 | |
| 28 | H264 DMA request | 0 | |

The board holds `0x0F506B95`. Two of its fields match hardware that is already known. The capacity field gives 64 MB, which is what the board carries, and the DDR bit is 1, which is the memory type it carries. Those two agreements are what makes the layout above trustworthy.

The programmed times are longer than the DRAM needs. 64 MB is a 512 Mb device, and JEDEC gives 72 ns as the minimum tRFC at that density, where the board programs 112.9 ns.

A new memory clock moves every time in the table above. Compute new field values from the DRAM minimums and the new clock.

## Refresh and delay register, +0x08

The board holds `0x00057C58`. Bits 19:16 are the DQS delay and bits 23:20 are the clock delay. The remaining bits carry the refresh interval, but nothing here fixes where that field starts. The low 16 bits are `0x7C58` on both device firmware versions and in every other configuration seen for this controller.

The DQS delay is 5 on the v1.58.2 device and 3 on the v1.88 device. That is the only difference between the two firmware versions in this register. It is a signal timing adjustment, not a refresh change.

## A clock change needs self refresh

The memory controller and the DRAM cannot follow a change of the memory clock while they run. Put the DRAM into self refresh first.

| Step | Write to `+0x00` |
| --- | --- |
| 1 | `0x40170000` |
| 2 | `0x00110000` |
| 3 | change the clock |
| 4 | `0x40170000` |
| 5 | `0x60170000` |

Step 4 raises CKE, which leaves self refresh. Step 5 turns normal operation back on.

With this sequence around an ASIC divider change from /2 to /4 and back, memory contents survive and the running image continues. Without it, the same change stops the board. The code that issues the sequence must run from L2 SRAM, because memory is unreachable between step 2 and step 4.

A change downward needs no new timing values, because a slower clock makes each field hold its delay for longer in real time. A change upward makes each delay shorter and needs new values first.

## DQS delay

Bits 19 to 16 of `+0x08` set where the controller samples read data. The field behaves like a delay line: at 124 MHz the values 0 to 7 all pass and 8 to 15 all fail completely. The window narrows as the clock rises, and the upper edge of that window is what stops the board.

| Memory clock | Clean values | Boot value 5 |
| --- | --- | --- |
| 124 MHz | 0 to 7 | inside |
| 150 MHz | 0 to 5 | at the edge |
| 160 MHz | 0 to 4 | one step outside |
| 170 MHz | none | far outside |

At 170 MHz no value is clean, and the values that were clean at 160 MHz give 2 to 7 errors each instead of a pass or a total failure. An error rate that does not follow the sampling phase comes from a different limit.

Bits 23 to 20 hold the clock delay, and the board boots with 0. At 170 MHz, raising it to 1 takes the best DQS value from those few errors to about 290, thus 0 is the right value for this board.

The highest memory clock that has never failed on the v1.58.2 device is 160 MHz, with the DQS delay moved off its boot value and the timing fields recomputed. That is 29 percent above the 124 MHz the board boots at. Neither change alone reaches it. The clocks between 160 and 170 MHz pass some runs and fail others, and a burst test does not separate them. See [the probe](../../baremetal/probes/cpufreq/README.md) for the measurements.

## Mode register

The boot time init issues three mode register commands through the command register.

| Word | Register | Contents |
| --- | --- | --- |
| `0x40104000` | extended, BA=1 | all zero, that is DLL on and normal drive |
| `0x40100123` | base, BA=0 | burst length 8, sequential, CAS latency 2, DLL reset |
| `0x40100023` | base, BA=0 | the same with DLL reset cleared |

The memory runs at CAS latency 2. Latency 3 is a different `A[6:4]` in the same command.

Bit 31 of the command register carries the controller side of the same choice. The correlation is exact across every command set seen for this controller. A set that loads CAS latency 3 in the mode register sets bit 31 in all of its command words, and a set that loads latency 2 clears it everywhere. Bit 31 therefore selects the CAS latency the controller expects, and it must agree with the mode register.

A change of CAS latency therefore needs both halves. The mode register alone puts the memory at one latency while the controller still expects the other, and every read then returns data off by a cycle.

The bit is easy to misread as a memory type select, because a DDR command set and an SDRAM one can differ in bit 31 and nothing else. Decode the mode register words next to a command set before concluding what the bit does.

## No divide by three

`Include/platform/AK7802/anyka_cpu_780x.h` in [Intrisit8000](https://github.com/DanielGit/Intrisit8000) documents `SYSCTRL+0x64` bit 28 as `asic = pll1_clock /3`. The AK98 driver puts its own 3X bit at `SYSCTRL+0x04` bit 28 instead and commits it with `PLL_CHANGE_ENA`.

Measurement rejects both. `SYSCTRL+0x64` bit 28 accepts a written 1 and drives no clock. `SYSCTRL+0x04` bit 28 does not accept a written 1 at all. Both strobes, `PLL_CHANGE_ENA` and `ASIC_AP_EN`, give the same result. See [the probe](../../baremetal/probes/cpufreq/README.md).

The ASIC divider is therefore a power of two only, and PLL1 cannot rise while the memory clock stays where it is. `ASIC_CLK = PLL1_CLK / 2^n` and `PLL1_CLK = 4 MHz * M / N`, with `M` from 62 to 94 and `N` at 1 on this board. See [docs/eboot/memory-map.md](../eboot/memory-map.md).

## Unresolved

Which bits of `+0x08` hold the refresh interval. The value never changes across the available material, thus it carries no information about the field position. A write and read back test at a known memory clock would settle it.
