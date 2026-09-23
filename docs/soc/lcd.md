# LCD controller

The AK7802 drives a display from one block at physical `0x20010000`. The block holds one scan out engine, and the engine takes its pixels from DDR by DMA. Three output interfaces exist, MPU, RGB and TV, and a two bit field selects between them. Four source layers exist, one RGB layer, one OSD layer and two YUV layers.

This board uses the RGB interface with the RGB layer alone. Everything below describes that path. Every register offset is relative to `0x20010000`.

The measurements come from [the LCD timing probe](../../baremetal/probes/lcd-timing/README.md).

## Registers

| Offset | Name | Holds |
| --- | --- | --- |
| `+0x00` | Interface control | Interface select, layer enables, FIFO alarm thresholds, pixel clock polarity |
| `+0x08` | Panel reset | Drives the panel reset pin |
| `+0x10` | RGB control | Data bus width, scan mode, sync polarity |
| `+0x14` | Frame buffer base | Virtual page enable, and the scan out address |
| `+0x18` | Virtual page size | Width and height of the page behind the picture |
| `+0x1C` | Virtual page offset | Position of the picture inside that page |
| `+0x20` | OSD base | OSD layer address |
| `+0x3C` | Background colour | Colour outside the picture |
| `+0x40` | Sync pulse widths | Horizontal and vertical |
| `+0x44` | Horizontal back porch and active width | |
| `+0x48` | Horizontal front porch and total | |
| `+0x4C` | Vertical back porch | |
| `+0x50` | Vertical front porch | |
| `+0x54` | Vertical active height | |
| `+0x58` | VSYNC output length | Not the vertical total, see below |
| `+0x5C`..`+0x98` | YUV layers | Two layers with scalers |
| `+0xA8` | Picture offset | Position of the picture on the panel |
| `+0xAC` | Picture size | |
| `+0xB0` | Display area size | |
| `+0xB8` | Operation | Scan out stop and start |
| `+0xBC` | Status | Refresh and error events |
| `+0xC0` | Interrupt enable | Same bit order as the status register |
| `+0xC8` | Software control | Commit strobe, fast DMA, scan line alarm |
| `+0xE8` | Pixel clock | Divider, clock enable, and a valid bit |

### Interface control, +0x00

| Bits | Meaning |
| --- | --- |
| 31:24 | FIFO empty alarm threshold in bytes |
| 23:16 | FIFO full alarm threshold in bytes |
| 15 | Swap red and blue |
| 6:5 | Interface: 1 MPU, 2 RGB, 3 TV |
| 4 | Pixel clock polarity, 1 is positive |
| 3:0 | Layer enables: RGB, YUV1, YUV2, OSD |

The empty alarm threshold arms the underrun detection described under [Scan out must outrank the core](#memory-arbitration).

### RGB control, +0x10

| Bits | Meaning |
| --- | --- |
| 22:21 | Data bus width: 0 is 8, 1 is 16, 2 is 18 |
| 20 | 1 selects progressive scan |
| 2:0 | HSYNC, VSYNC and gate polarity, 1 is negative |

### Panel timing, +0x40 to +0x58

Horizontal fields count pixel clocks. Vertical fields count lines.

| Register | Bits | Field |
| --- | --- | --- |
| `+0x40` | 23:12 | HSYNC pulse width |
| `+0x40` | 11:0 | VSYNC pulse width |
| `+0x44` | 23:12 | Horizontal back porch |
| `+0x44` | 11:0 | Horizontal active width |
| `+0x48` | 25:13 | Horizontal front porch |
| `+0x48` | 12:0 | Horizontal total |
| `+0x4C` | 11:0 | Vertical back porch |
| `+0x50` | 11:0 | Vertical front porch |
| `+0x54` | 26:15 | Vertical active height |
| `+0x58` | 12:0 | VSYNC output length |

### Picture placement, +0xA8 to +0xB0

`+0xA8` holds the vertical offset in bits 19:10 and the horizontal offset in bits 9:0. `+0xAC` and `+0xB0` both hold `(width << 10) | height`. `+0xAC` sizes the RGB layer and `+0xB0` sizes the display area.

### Status, +0xBC

| Bit | Event |
| --- | --- |
| 18 | FIFO empty alarm |
| 17 | Scan line alarm |
| 8, 7 | Even field start, even field done |
| 6, 5 | Odd field start, odd field done |
| 4, 3 | Refresh start, refresh done |
| 2, 1 | MPU refresh done, MPU refresh start |
| 0 | System error |

Every bit clears on read. Two reads in a row give the accumulated events and then zero. Thus a poll loop and an interrupt handler cannot both see the same event, because the first read clears it.

`+0xC0` enables an interrupt per bit in the same order.

### Software control, +0xC8

| Bit | Meaning |
| --- | --- |
| 17 | Fast DMA |
| 11 | Commit strobe |
| 10 | Arm the scan line alarm |
| 9:0 | Scan line for that alarm |

The register reads back as zero. Software that needs to change one bit must therefore keep the whole value itself. A read-modify-write drops every other bit.

## Pixel clock

```
pixel clock = PLL1 / (2 * (divider + 1))
```

The divider is bits 7:1 of `+0xE8`, seven bits wide. Bit 8 enables the clock and bit 0 marks the divider valid. PLL1 is 248 MHz on this board, thus the dividers that give a usable rate on this panel are:

| Divider | Pixel clock | Refresh on this panel | Scan out reads |
| --- | --- | --- | --- |
| 2 | 41.33 MHz | 77.1 Hz | 59.2 MB/s |
| 3 | 31.00 MHz | 57.8 Hz | 44.4 MB/s |
| 4 | 24.80 MHz | 46.2 Hz | 35.5 MB/s |
| 8 | 13.78 MHz | 25.7 Hz | 19.7 MB/s |

## Frame length

The vertical total is `active + front porch + sync pulse + back porch`. `+0x58` does not take part. It sets the length of the VSYNC output signal, and on this board it holds 505 while the frame is 508 lines.

A driver that takes the vertical total from `+0x58` instead reads 505 on this board and computes a refresh rate 0.6 percent high.

## Commit strobe

A register change reaches the panel only after a write of one to bit 11 of `+0xC8`. This applies to a new frame buffer base, a new picture offset and new timing. Without the strobe the register holds the new value and scan out keeps using the old one.

The bit means "the registers for the next frame are ready". It is a strobe, not a level: writing one sets it, and the hardware clears it.

## Panning

To scroll the picture, add `rows * stride` to the address in `+0x14` and strobe. Nothing else moves.

The virtual page path does not do this. `+0x14` bit 28 enables it, `+0x18` sizes the page and `+0x1C` is meant to hold the offset inside it, but a page of 800 by 1440 with a non-zero vertical offset shows memory from outside the page instead of shifting the picture. The AK98 fbdev driver also pans by the base address and never enables the virtual page.

## Memory arbitration

Clear the low seven bits of `0x2002D014` during bring-up. That register sits outside this block, in the RAM controller. It reads back what it is given and resets to all ones, so a driver that clears the field writes `0xFFFFFF80`.

Leave the bits set and scan out dies under memory load. Load alone is not enough: the scan out rate has to be high enough for the FIFO to run dry before the DMA is served again.

| Scan out reads | Low seven bits clear | Low seven bits set |
| --- | --- | --- |
| 19.7 MB/s | survives | survives |
| 35.5 MB/s | survives | not measured |
| 44.4 MB/s | survives | not measured |
| 59.2 MB/s | survives | fails |

The failure sets bit 18 and bit 0 of the status register, and then scan out stops. Bit 18 fires when the FIFO falls below the threshold in bits 31:24 of `+0x00`, which is 128 bytes on this board. Scan out does not restart when the load ends, and it does not restart when the bits go back to clear. Until it is restarted the panel holds whatever the last complete frame left.

Clearing the field costs the core nothing. It gains: with the bits clear the core completes 5.7 percent more work in the same time.

### Restarting scan out

Write bit 0 of `+0xB8` to stop, wait one frame, then write bit 2 to start. A commit strobe does not work, and neither does rewriting the frame buffer base.

Do not skip the wait. Starting again during the active area restarts the pixel stream at an arbitrary phase, which shows as a picture shifted sideways and wrapped.

## Frame buffer mapping

Scan out DMA does not snoop the D-cache, so the frame buffer needs a mapping without it. ARMv5 offers two, and they write at 50 and 146 MiB/s:

| Mapping | Write | Read |
| --- | --- | --- |
| Non-cacheable, non-bufferable | 50 MiB/s | 42.7 MiB/s |
| Non-cacheable, bufferable | 146 MiB/s | 42.7 MiB/s |

Prefer the bufferable mapping and drain the write buffer before the commit strobe. Reads are the same either way, because the bufferable bit only covers writes.

## Clock gate and reset

`SYSCTRL+0x0C` bit 3 gates the controller clock, and the polarity is inverted: clear the bit to run. Bit 19 of the same register is a reset strobe. Both need a read-modify-write, because the register holds gates for other blocks.

## On this board

### Panel

The panel is 800 by 480, RGB565 over a 16 bit bus, progressive. HSYNC and VSYNC are active low and the gate is active high. The pixel clock is positive edge.

| Field | Horizontal | Vertical |
| --- | --- | --- |
| Active | 800 | 480 |
| Front porch | 40 | 1 |
| Sync pulse | 128 | 3 |
| Back porch | 88 | 24 |
| Total | 1056 | 508 |

The device firmware carries this timing under the name `auo`. It also names a pixel clock of 25.5 MHz, which no divider reaches exactly. Divider 4 is the nearest, at 24.80 MHz, 2.7 percent under.

The panel also holds a picture at 41.33 MHz.

### Backlight

A single channel PWM generator at `SYSCTRL+0x2C` drives the backlight. Bits 31:16 count the high time and bits 15:0 count the low time. The source is a fixed 12 MHz and does not follow the PLL, thus:

```
period cycles = 12000000 / period_hz
high          = duty_percent * period_cycles / 100
low           = period_cycles - high
```

A duty of 100 percent encodes as high `0xFFFF` and low zero. A duty of zero encodes as both zero. The boot firmware on this board sets 1 kHz at 70 percent, which is `0x20D00E10`.

### Refresh rate and core bandwidth

Scan out reads DDR the whole time the panel is on, so a faster pixel clock takes bandwidth from the core. Against divider 8:

| Divider | Refresh | Core is slower by |
| --- | --- | --- |
| 4 | 46.2 Hz | 9.2 percent |
| 3 | 57.8 Hz | 14.2 percent |
| 2 | 77.1 Hz | 24.1 percent |

## Unresolved

- The OSD layer and the two YUV layers have register definitions but no measurement.
- Why the virtual page offset at `+0x1C` reads outside the page. The field position and the unit both come from the AK98 source and neither matches what the part does.
- What the low seven bits of `0x2002D014` select. Clearing them both protects scan out and speeds up the core, which a strict priority does not explain.
