# LCD timing

This probe measures the LCD controller on the v1.58.2 device: the pixel clock, the frame length, the panning path, the framebuffer mapping, and the memory arbitration that keeps scan out alive under load. The rules that follow from these measurements are in [docs/soc/lcd.md](../../../docs/soc/lcd.md). This file holds the measurements themselves.

The payload is a GDB stub with its `main.c` replaced. The measurements run before USB starts. Then the normal stub body runs, so the operator can poke registers by hand.

The current payload reproduces most of the numbers below. Some numbers come from earlier builds of this payload, and the section that gives each of them says so.

## Build and run

```sh
make -C baremetal/probes/lcd-timing/payload
```

Copy `BOOT.BIN` to the root of the first FAT partition of the SD card and boot. Then read the log:

```sh
arm-none-eabi-gdb -ex 'target remote /dev/cu.usbmodem*' -ex 'monitor trace'
```

The same numbers are also at `0x32008000` as raw words, in the order that the log prints them.

The log prints these sections in this order:

| Log section | Content |
| --- | --- |
| `ahb priority 0x2002D014` | Read back of `0x2002D014` after three writes |
| `status 0x2001,00BC` | Two reads of the status register, then the read back after a write of all ones and of zero |
| `boot divider, no clear`, `boot divider, w1c` | Refresh edges at divider 8 over 2 s, without and with a write back of the status bits |
| `divider sweep` | Refresh edges at divider 8 and divider 4 over 2 s |
| `soft ctrl 0xC8 read back` | Read back of `+0xC8` |
| `phase 2, MMU on` | Framebuffer throughput, the priority curve, the load margin, and the recovery test |
| `page painted` | The page for manual panning, and the two writes that pan it |

The log uses these labels:

| Label | Meaning |
| --- | --- |
| `field` | The divider in bits 7:1 of `+0xE8` |
| `ok_edges`, `start_edges` | Low to high transitions of status bit 3 and bit 4 |
| `ok_high` | Samples with status bit 3 set, over all samples |
| `cHz` | Refresh rate in hundredths of a hertz, from `ok_edges` |
| `low7` | The low seven bits of `0x2002D014` during the timed work. Bit 7 and above are set. |
| `ticks` | Timer 1 ticks for the timed work. Fewer ticks means that the core got more of the bus. |
| `status_or` | The OR of every status read during the timed work |
| `edges_after`, `before`, `after` | Refresh edges over 200 ms. Zero means that scan out stopped. |
| `step` | Recovery action: 0 none, 1 commit strobe, 2 stop and start, 3 base rewrite and commit strobe, 4 full bring-up |

The payload changes this state and does not restore it:

- `0x2002D014` holds `0xFFFFFF80`.
- `+0xC0` enables status bit 18 and bit 0. Only the status bits latch, because the interrupt mask in SYSCTRL and the CPSR I bit stay closed.
- The divider is 8 and scan out runs.
- An 800 by 1440 page fills the framebuffer, with a black line every 48 rows.

The throughput and starvation tests turn on the MMU, because they need the caches. The payload turns the MMU off again before USB starts. MUSB is at `0x70000000`, and the probe map does not cover it. All other parts run with the MMU off.

## Method

Timer 1 is the time base. It runs from the 12 MHz crystal and does not follow PLL1, but the pixel clock does. Thus a measurement against timer 1 does not depend on the clock that it measures.

The refresh rate comes from the count of low to high transitions of status bit 3 over a fixed window. The status register clears on read, so one read per sample is enough.

The load is a fixed amount of work, and timer 1 measures how long it takes. One pass does 2048 read-modify-writes over 4 MB of cached DDR, one 32 byte cache line per access, then 2048 word writes into the framebuffer. The timed work is 256 passes.

## Results

### The divider carries a plus one

The pixel clock is `PLL1 / (2 * (divider + 1))`. An earlier build swept nine dividers with 2 s windows. The current payload measures divider 8 and divider 4 only.

| Divider | Measured | Predicted with plus one, 1056 x 508 frame |
| --- | --- | --- |
| 2 | 77.0 Hz | 77.05 Hz |
| 3 | 57.5 Hz | 57.79 Hz |
| 4 | 46.5 Hz | 46.23 Hz |
| 5 | 38.5 Hz | 38.53 Hz |
| 6 | 33.0 Hz | 33.02 Hz |
| 7 | 29.0 Hz | 28.89 Hz |
| 8 | 25.5 Hz | 25.68 Hz |
| 10 | 21.0 Hz | 21.01 Hz |
| 12 | 18.0 Hz | 17.78 Hz |

The frame length is constant during the sweep, because the sweep writes no timing register. Divide each pixel clock by `1056 *` the rate measured at that divider, and the result is the frame length in lines. With the plus one, the nine results give 507.6 lines with a standard deviation of 2.9. Without it, the results move from 543 to 762 lines, with a standard deviation of 68.9. One frame of quantization in a 2 s window explains the 2.9.

A second argument excludes the reading without the plus one and does not use the fit. At divider 4, that reading gives 31.00 MHz and 58.1 Hz. The plus one gives 24.80 MHz, and at 24.80 MHz no frame can be faster than `24.8e6 / (1056 * 480) = 48.9 Hz`, because a frame cannot be shorter than its active area. The measured 46.5 Hz is below that ceiling.

The same data shows one status bit 3 edge per frame. One edge for two frames gives a frame of 254 lines, which is shorter than the 480 active lines. Two edges per frame gives 1016 lines, which the porch registers do not allow.

### The frame is 508 lines, not the 505 in `+0x58`

The 507.6 lines from the sweep is 0.08 percent from 508. 508 is the sum of the four vertical fields: 480 active, 1 front porch, 3 sync pulse and 24 back porch. `+0x58` holds 505. A fit with a 505 line frame puts the nine dividers about 0.7 percent low as a group.

### `0x2002D014` is ordinary read write storage

A write of `0xFFFFFFFF` reads back `0xFFFFFFFF`, and a write of `0xFFFFFF80` reads back `0xFFFFFF80`. A read before any write gives all ones, as [docs/soc/memory-controller.md](../../../docs/soc/memory-controller.md) records, thus all ones is the reset value.

### `+0xBC` clears on read and `+0xC8` reads back zero

Two reads of `+0xBC` in a row give `0x000601F8` and then `0x00000000`. `+0xC8` reads back zero after every write.

### Panning works through the base address, not the virtual page

The operator added one 48 row band per second to the base in `+0x14` from GDB, with a commit strobe after each write. The eight colored bands moved in order, with no tearing.

The virtual page does not pan. With `+0x14` bit 28 set, an 800 by 1440 page in `+0x18`, and a non-zero vertical offset in `+0x1C`, the panel shows DRAM that the payload never painted. Without the commit strobe, the picture does not change at all.

### The bufferable mapping writes 2.9 times faster

One 800 by 480 span, with the MMU on:

| Mapping | Write | Read |
| --- | --- | --- |
| Non-cacheable, non-bufferable | 50 MiB/s | 42.7 MiB/s |
| Non-cacheable, bufferable | 146 MiB/s | 42.7 MiB/s |

The non-bufferable write took 176450 to 177589 ticks over four runs. The bufferable write printed 150000 KiB/s in three runs. One other run printed 169811 KiB/s, because it measured after scan out stopped and the core had the bus alone. The current payload restarts scan out before this measurement.

The two reads are equal, and they must be, because the bufferable bit affects only writes. This agreement is a check on the pair.

An earlier build measured with the MMU off and got 32.7 MiB/s write and 5.9 MiB/s read. Those numbers measure uncached instruction fetch, not the framebuffer. The same read loop ran seven times faster with the MMU on.

### Scan out dies under load only at a high refresh rate

With the low seven bits of `0x2002D014` set, scan out stops under load at divider 2 but not at divider 8. With the bits clear, it survives at every divider that the payload tried.

| Divider | Scan out reads | Bits clear | Bits set |
| --- | --- | --- | --- |
| 8 | 19.7 MB/s | survives | survives |
| 4 | 35.5 MB/s | survives | not measured |
| 3 | 44.4 MB/s | survives | not measured |
| 2 | 59.2 MB/s | survives | fails |

The failure below comes from a build that wrote `0xFFFFFF00`, that is with bit 7 clear. Divider 2, forwards then backwards:

```
low7=00 ticks=3991572 status_or=000201F8
low7=7F ticks=2969409 status_or=00040081
low7=7F ticks=2968915 status_or=00000000
low7=00 ticks=2968910 status_or=00000000
```

`0x00040081` has bit 18, the FIFO empty alarm, and bit 0, the system error. After it, the status register reads zero on every sample, also after the bits go back to clear. The core finishes the same work 25 percent faster, because the DMA no longer reads DDR.

Three earlier builds found no failure, because all of them ran at divider 8. At that rate neither the fast DMA bit, nor the priority bits, nor a framebuffer write stream in the load changed `status_or` from `0x000201F8`.

In the current payload, with bits clear and under load, the edges over 200 ms after the load agree with the refresh rate at each divider:

| Divider | Ticks | `status_or` | Edges | Expected |
| --- | --- | --- | --- | --- |
| 4 | 3396291 | `000201F8` | 10 | 9.2 |
| 3 | 3551617 | `000201F8` | 11 | 11.6 |
| 2 | 3861866 | `000201F8` | 15 | 15.4 |

### Clearing the low seven bits makes the core faster

At divider 8 the current payload takes 3111652 ticks with the bits clear and 3289200 with the bits set, forwards and backwards to cancel drift. Thus the core finishes 5.7 percent sooner with the bits clear.

Bit 7 also has an effect. With the low seven bits clear, the build that wrote `0xFFFFFF00` took 3255302 ticks, and the current payload with `0xFFFFFF80` takes 3111009. That is 4.4 percent faster with bit 7 set.

### Only a stop and start restarts scan out

Each action got its own forced failure, and the payload ran a full bring-up between two actions. The payload restored `0x2002D014` first, then did one action, then counted edges over 200 ms.

| Action | Edges before | Edges after |
| --- | --- | --- |
| None | 0 | 0 |
| Commit strobe | 0 | 0 |
| `+0xB8` stop, 50 ms, start | 0 | 16 |
| Rewrite the base and commit strobe | 0 | 0 |
| Full bring-up | 0 | 5 |

The five zeros before each action show that the forced failure is reliable. Each count after a successful action agrees with the refresh rate at that time. The stop and start ran at divider 2 and gave 16 against 15.4 expected. The full bring-up sets divider 8 and gave 5 against 5.1 expected.

The full bring-up also ends with a write of the start bit to `+0xB8`.

### A higher refresh rate slows the core

The ticks for the same work under load come from the load margin test above, against 3111009 ticks at divider 8:

| Divider | Refresh | Ticks | Core is slower by |
| --- | --- | --- | --- |
| 4 | 46.2 Hz | 3396291 | 9.2 percent |
| 3 | 57.8 Hz | 3551617 | 14.2 percent |
| 2 | 77.1 Hz | 3861866 | 24.1 percent |

### The panel holds a picture at 41.33 MHz

At divider 2 the operator examined the band page on one unit: flat colors and single pixel black lines. The colors were correct, the lines were sharp, and no column was missing or unstable.
