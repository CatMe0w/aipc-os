# LCD Driver

EBOOT configures the on-chip LCD controller for an 800x480 RGB565 TFT
panel. It also drives the panel backlight through a single-channel PWM
generator exposed by SYSCTRL. This document records the register values
and sequence needed to reproduce what EBOOT does.

Unless otherwise noted, the sequence below is taken directly from
`lcd_init` assembly. Where the code uses read-modify-write on a register,
the cold-boot-equivalent final literal is shown.

The LCD controller base is physical `0x20010000`, uncached virtual
`0xA8010000` through `OALPAtoVA`. All register offsets below are relative
to that base.

## Controller Register Map

| Offset | Value observed in EBOOT | Description                                       |
| ------ | ----------------------- | ------------------------------------------------- |
| +0x00  | `0x80A80058` (final)    | Main control register; see *Control Register* below |
| +0x10  | `0x00300006`            | H timing config 1 `[partial]`                     |
| +0x14  | `0x07B00000`            | Framebuffer base register literal (see note)      |
| +0x18  | `0x032001E0`            | Stride / per-line layout word `[partial]`         |
| +0x3C  | `0x00000000`            | Cleared during init                               |
| +0x40  | `0x00080003`            | V timing `[partial]`                              |
| +0x44  | `0x00058320`            | H sync `[partial]`                                |
| +0x48  | `0x00050420`            | V sync `[partial]`                                |
| +0x4C  | `0x00000018`            | Porch / 24 pixels `[partial]`                     |
| +0x50  | `0x00000001`            | Enable flag `[partial]`                           |
| +0x54  | `0x00F00000`            | Resolution-related `[partial]`                    |
| +0x58  | `0x000001F9`            | 505 (V total)                                     |
| +0xA8  | `0x00000000`            | Cleared                                           |
| +0xAC  | `0x000C81E0`            | `[partial]`                                       |
| +0xB0  | `0x000C81E0`            | `[partial]`                                       |
| +0xB8  | bit 0 clear, bit 2 set  | `[partial]`                                       |
| +0xC8  | bit 11 set              | `[partial]`                                       |
| +0xE8  | `0x00000111`            | Pixel clock divider (see below)                   |

Most fields marked `[partial]` have the correct literal value listed but
their bit-level meaning has not been reverse-engineered from the LCD
controller perspective. The values are directly lifted from EBOOT's
`lcd_init` and are guaranteed to produce a working display for the
on-board panel at 248 MHz CPU clock; changing the panel or the CPU
clock would require deriving new values.

### Control Register (+0x00)

The main control word is built up in three write phases. The final
value is `0x80A80058`.

Known bit assignments in the final value:

- bit 3 (`0x08`): start / refresh enable `[partial]`
- bit 4 (`0x10`): DMA enable `[partial]`
- bit 6 (`0x40`): mode bit set during phase 1 `[partial]`
- bits 19, 21, 23: set in the final value, meaning `[unknown]`
- bit 31 (`0x80000000`): main controller enable

EBOOT writes the control register three times in order, interleaved
with other register writes. The precise sequence matters: writing the
final value in one step has not been observed to work.

See the *Bring-Up Sequence* section below for the exact ordering.

### Framebuffer Base (+0x14)

EBOOT writes the literal `0x07B00000` into `+0x14` after first masking
off the previous high nibble. What `lcd_init` directly proves is only:

- CPU-side framebuffer clears target cached virtual `0x87B00000`
- the LCD controller register receives `0x07B00000`

The commonly used physical interpretation `0x33B00000` comes from the
platform's 64 MB DDR wrap behavior and observed working display state,
not from an explicit comment or symbolic field decode inside
`lcd_init` itself.

On current hardware, the effective framebuffer region is treated as a
5 MB area starting at `0x33B00000`: `800 * 480 * 2 = 768000` bytes are
live pixels, and the region is rounded up to 5 MB to give some
headroom.

### Pixel Clock Divider (+0xE8)

EBOOT writes `0x00000111` into `+0xE8`. Given CPU clock of 248 MHz and
target pixel clock of 25.5 MHz, the divider that produces this value
follows the formula:

```
div = (cpu_clk / pix_clk) - 1
+0xE8 = 2 * (div & 0x7F) | 0x101
```

For CPU 248 MHz and pixel 25.5 MHz: `div = 8`, `2 * 8 | 0x101 = 0x111`.
The `|0x101` mask is always set; only the `2 * div` part varies with
clock selection.

LCD pixel clock is also configured through a separate PAL IOCTL with
ID `0x010120EC` and payload `0x30` during LCD init. In the verified
instruction order, EBOOT writes the controller-local divider at `+0xE8`
first and issues the PAL IOCTL immediately afterwards.

## Panel Timing

The panel is 800x480 active with a total blanking dimension of
1056 x 505:

```
H_active  = 800           H_total = 1056         H_blanking = 256
V_active  = 480           V_total = 505          V_blanking =  25

Pixel clock = 25.5 MHz
Frame rate  = 25_500_000 / (1056 * 505) = 47.82 Hz
```

These are the derived values; EBOOT does not advertise them as such,
but they match what the PAL IOCTL path and the timing registers
configure. 47.82 Hz is typical for a cheap 800x480 TFT running at
~25 MHz pixel clock.

## Bring-Up Sequence

The complete init sequence performed by `lcd_init`, in order:

```c
// 1. Clock and reset pulse.
*LCD(0x3C) = 0;
*SYSCTRL(0x0C) |=  (1 << 19);    // assert LCD reset
*SYSCTRL(0x0C) &= ~(1 << 19);    // deassert (pulse)
*SYSCTRL(0x0C) &= ~(1 <<  3);    // enable LCD clock (inverted polarity)

// 2. Clear 5 MB of framebuffer memory.
memset(fb_virt, 0, 5 * 1024 * 1024);

// 3. Program the pixel clock divider and issue the PAL IOCTL.
*LCD(0xE8) = 0x00000111;
pal_ioctl(0x010120EC, &value_0x30, 4, 0, 0, 0);

// 4. Ensure the LCD pad routing is enabled.
//    hw_phase1_init already enables alt IDs 44, 8, 53, 13, 12, 16, 51, 52.
//    lcd_init itself re-enables only alt ID 51.
gpio_enable_alt(51);

// 5. Control phase 1.
*LCD(0x00) = 0x00000040;         // actual code uses RMW

// 6. Timing registers.
*LCD(0x10) = 0x00300006;
*LCD(0x40) = 0x00080003;
*LCD(0x44) = 0x00058320;
*LCD(0x48) = 0x00050420;
*LCD(0x4C) = 0x00000018;
*LCD(0x50) = 0x00000001;
*LCD(0x54) = 0x00F00000;
*LCD(0x58) = 0x000001F9;

// 7. Control phase 2: main enable + mode.
*LCD(0x00) = 0x80A80050;         // actual code uses RMW

// 8. Layout / framebuffer base.
*LCD(0xB0) = 0x000C81E0;
*LCD(0x14) = 0x07B00000;         // actual code first preserves the high nibble
*LCD(0x18) = 0x032001E0;
*LCD(0xA8) = 0;
*LCD(0xAC) = 0x000C81E0;

// 9. Control phase 3: start refresh.
*LCD(0x00) |= 0x08;              // final value 0x80A80058

// 10. Trailing config bits.
*LCD(0xC8) |= 0x800;
*LCD(0xB8) = (*LCD(0xB8) & ~1) | 4;
```

The ordering is reproduced from `lcd_init` and should be followed
literally. The critical points:

- `SYSCTRL+0x0C` bit 3 is inverted polarity: **clear to enable**.
- `SYSCTRL+0x0C` bit 19 is a pulse; toggle high then low.
- `LCD+0x00` is written three times during init, not once.
- `pal_ioctl(0x010120EC, &0x30, 4, 0, 0, 0)` occurs before the first
  `LCD+0x00` control write.
- Timing registers must be written between control phase 1 and
  control phase 2.
- Layout registers (`+0x14`, `+0x18`, `+0xB0`, `+0xAC`) must be
  written between phase 2 and phase 3.
- Backlight PWM routing and `pwm_set(1000, 70)` are not part of
  `lcd_init`; they are performed later by `oem_platform_init`.

## Backlight PWM

### PWM Register

A single-channel PWM generator lives at `SYSCTRL + 0x2C`:

```
bits 31..16: high_time cycles (on duration)
bits 15..0:  low_time  cycles (off duration)
```

The PWM source clock is **12 MHz, fixed**, and does not vary with CPU
clock or PLL configuration. One cycle equals 1/12 microsecond.

### `pwm_set(period_hz, duty_pct)`

EBOOT's helper takes a period frequency and a duty percent and
computes the register value as:

```
period_cycles = 12_000_000 / period_hz
high_cycles   = duty_pct        * period_cycles / 100
low_cycles    = (100 - duty_pct) * period_cycles / 100

SYSCTRL(0x2C) = low_cycles | (high_cycles << 16)
```

`oem_platform_init` calls `gpio_enable_alt(20)` and then
`pwm_set(1000, 70)` after `lcd_init` returns:

```
period_cycles = 12_000_000 / 1000 = 12000
high_cycles   = 70 * 12000 / 100  = 8400   = 0x20D0
low_cycles    = 30 * 12000 / 100  = 3600   = 0x0E10

SYSCTRL(0x2C) = 0x0E10 | (0x20D0 << 16) = 0x20D00E10
```

That produces a 1 kHz backlight PWM at 70% duty. 100% duty is encoded
as `high = 0xFFFF, low = 0`, and 0% duty is `high = 0, low = 0`.

### PWM Routing

The PWM generator's output must be routed to a pad through the alt
function mux. `oem_platform_init` does this via `gpio_enable_alt(20)`,
where `20` is an **alt function ID**, not a physical pin number. The
specific alt function that ID `20` enables is inferred to be the PWM
pad routing targeting physical `GPIO1[9]` (which the bootrom GPIO
crosswalk identifies as `WLED_PWM`). See the `Unresolved` section
below for the caveat.

## Framebuffer Placement

EBOOT clears 5 MB at cached virtual `0x87B00000` and programs the LCD
controller with the literal `0x07B00000`. On current hardware this
configuration corresponds to the wrapped DDR framebuffer region usually
described as physical `0x33B00000`, but that physical interpretation
comes from platform address-wrap behavior rather than from `lcd_init`
alone.

Pixel format is RGB565 (16 bpp), so one line is `800 * 2 = 1600`
bytes and the whole active framebuffer is `1600 * 480 = 768000` bytes.

This framebuffer location is valid only for the boot path that runs
EBOOT to completion and hands off to software that inherits the LCD
controller state. Once WinCE's display driver takes over, it
allocates its own framebuffer at a runtime-determined address and
writes a new value into `LCD+0x14`. A Linux consumer that observes
the LCD state after EBOOT will see `0x33B00000`; a Linux consumer
that boots via HaRET warmboot after WinCE will see whatever NK
allocated (observed as `0x33ED3C00` on test units).

Shipping a stable framebuffer layout for Linux requires a real driver
that owns the LCD controller and programs `+0x14` itself, rather than
inheriting whatever `lcd_init` or WinCE left behind.

## Unresolved

- The meaning of `+0x10`, `+0x18`, `+0x40..+0x58`, `+0xA8..+0xC8`, and
  `+0xE8` bits is only known to the extent of "these values work for
  the 800x480 panel at 25.5 MHz pixel clock with CPU 248 MHz". The
  individual bit assignments were not reverse-engineered.
- The alt function ID `20` routing: inferred to drive `GPIO1[9] =
  WLED_PWM` but not confirmed by walking the per-alt stub for that ID
  and cross-referencing the sharepin bit against a pin mapping. See
  [gpio-driver.md](gpio-driver.md) for the alt-ID-to-physical-pin
  problem in general.
- The exact meaning of PAL IOCTL `0x010120EC` with payload `0x30`
  during LCD clock setup is not yet decoded. A conservative reading is
  that EBOOT performs two separate clock-related steps for LCD bring-up:
  this PAL IOCTL plus the controller-local divider write at `LCD+0xE8`.
  Their precise division of responsibility is not yet confirmed.
- Whether the LCD controller uses bit 31 of `+0x14` for anything,
  and whether writing the full 32-bit `0x07B00000` is necessary or
  whether the low 28 bits alone suffice, is not confirmed.
- Stable stuck-pixel test patterns, blanking behavior, and dynamic
  resolution changes have not been exercised. The documentation
  describes steady-state operation only.
