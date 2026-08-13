# LCD Driver

EBOOT configures the on-chip LCD controller for an 800x480 RGB565 TFT panel. It also drives the panel backlight through a single-channel PWM generator in SYSCTRL. This document records the register values and the sequence that reproduce what EBOOT does.

The sequence below comes directly from the `lcd_init` assembly, unless the text says otherwise. Where the code uses a read-modify-write on a register, the cold-boot-equivalent final literal appears instead.

The LCD controller base is physical `0x20010000`, uncached virtual `0xA8010000` through `OALPAtoVA`. Every register offset below is relative to that base.

## Controller Register Map

| Offset | Value observed in EBOOT | Description |
| --- | --- | --- |
| +0x00 | `0x80A80058` (final) | Main control register; see _Control Register_ below |
| +0x10 | `0x00300006` | H timing config 1 `[partial]` |
| +0x14 | `0x07B00000` | Framebuffer base register literal (see note) |
| +0x18 | `0x032001E0` | Active size word: `(width << 16) | height` |
| +0x3C | `0x00000000` | Cleared during init |
| +0x40 | `0x00080003` | V timing `[partial]` |
| +0x44 | `0x00058320` | H sync `[partial]` |
| +0x48 | `0x00050420` | V sync `[partial]` |
| +0x4C | `0x00000018` | Porch / 24 pixels `[partial]` |
| +0x50 | `0x00000001` | Enable flag `[partial]` |
| +0x54 | `0x00F00000` | Resolution-related `[partial]` |
| +0x58 | `0x000001F9` | 505 (V total) |
| +0xA8 | `0x00000000` | Cleared |
| +0xAC | `0x000C81E0` | Packed active size: `(width << 10) | height` |
| +0xB0 | `0x000C81E0` | Packed active size: `(width << 10) | height` |
| +0xB8 | bit 0 clear, bit 2 set | `[partial]` |
| +0xC8 | bit 11 set | `[partial]` |
| +0xE8 | `0x00000111` | Pixel clock divider (see below) |

Most fields marked `[partial]` carry the correct literal value, but their bit-level meaning is not reverse-engineered from the LCD controller side. The values come straight from `lcd_init` in EBOOT, and they give a working display for the on-board panel at a 248 MHz CPU clock. A different panel or a different CPU clock needs new values. The WinCE display driver corroborates the size words at `+0x18`, `+0xAC` and `+0xB0`. See [NK Display Driver](../nk/display-driver.md).

### Control Register (+0x00)

The main control word goes out in three write phases. The final value is `0x80A80058`.

Known bit assignments in the final value:

- bit 3 (`0x08`): set in the final phase to start the controller `[partial]`
- bit 4 (`0x10`): display path enable bit `[partial]`
- bit 6 (`0x40`): mode bit set during phase 1 `[partial]`
- bits 19, 21, 23: set in the final value, meaning `[unknown]`
- bit 31 (`0x80000000`): main controller enable

EBOOT writes the control register three times in order, with other register writes in between. The order matters. A single write of the final value has never worked in a test.

See the _Bring-Up Sequence_ below for the exact order.

### Framebuffer Base (+0x14)

EBOOT writes the literal `0x07B00000` into `+0x14`, after it masks off the previous high nibble. `lcd_init` proves only two things directly:

- the CPU-side framebuffer clears target cached virtual `0x87B00000`
- the LCD controller register receives `0x07B00000`

The WinCE display driver later uses the same low-28-bit form for a framebuffer base, thus do not treat the high nibble as part of the framebuffer address. The common physical interpretation `0x33B00000` comes from the 64 MB DDR wrap behavior of the platform and from the observed working display state. It does not come from a comment or a symbolic field decode inside `lcd_init`.

On current hardware, the effective framebuffer region is a 5 MB area from `0x33B00000`. Live pixels take `800 * 480 * 2 = 768000` bytes, and the region rounds up to 5 MB for some headroom.

### Pixel Clock Divider (+0xE8)

EBOOT writes `0x00000111` into `+0xE8`. For a CPU clock of 248 MHz and a target pixel clock of 25.5 MHz, this formula gives that value:

```
div = (cpu_clk / pix_clk) - 1
+0xE8 = 2 * (div & 0x7F) | 0x101
```

For CPU 248 MHz and pixel 25.5 MHz: `div = 8`, and `2 * 8 | 0x101 = 0x111`. The `|0x101` mask is always set. Only the `2 * div` part changes with the clock selection.

A separate PAL IOCTL, ID `0x010120EC` with payload `0x30`, also configures the LCD pixel clock during the LCD init. In the verified instruction order, EBOOT writes the controller-local divider at `+0xE8` first and issues the PAL IOCTL immediately after.

## Panel Timing

The panel is 800x480 active, with a total blanking dimension of 1056 x 505:

```
H_active  = 800           H_total = 1056         H_blanking = 256
V_active  = 480           V_total = 505          V_blanking =  25

Pixel clock = 25.5 MHz
Frame rate  = 25_500_000 / (1056 * 505) = 47.82 Hz
```

These are derived values. EBOOT does not advertise them, but they match what the PAL IOCTL path and the timing registers configure. 47.82 Hz is typical for a cheap 800x480 TFT at a pixel clock of about 25 MHz.

## Bring-Up Sequence

The complete init sequence of `lcd_init`, in order:

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

This order comes from `lcd_init`, and a port must keep to it literally. The critical points:

- `SYSCTRL+0x0C` bit 3 has inverted polarity. **Clear it to enable.**
- `SYSCTRL+0x0C` bit 19 is a pulse. Toggle it high, then low.
- `LCD+0x00` takes three writes during init, not one.
- `pal_ioctl(0x010120EC, &0x30, 4, 0, 0, 0)` comes before the first `LCD+0x00` control write.
- The timing registers must go out between control phase 1 and control phase 2.
- The layout registers (`+0x14`, `+0x18`, `+0xB0`, `+0xAC`) must go out between phase 2 and phase 3.
- The backlight PWM routing and `pwm_set(1000, 70)` are not part of `lcd_init`. `oem_platform_init` does them later.

## Backlight PWM

### PWM Register

A single-channel PWM generator lives at `SYSCTRL + 0x2C`:

```
bits 31..16: high_time cycles (on duration)
bits 15..0:  low_time  cycles (off duration)
```

The PWM source clock is **12 MHz, fixed**. It does not change with the CPU clock or the PLL configuration. One cycle is 1/12 microsecond.

### `pwm_set(period_hz, duty_pct)`

The EBOOT helper takes a period frequency and a duty percent, and computes the register value:

```
period_cycles = 12_000_000 / period_hz
high_cycles   = duty_pct        * period_cycles / 100
low_cycles    = (100 - duty_pct) * period_cycles / 100

SYSCTRL(0x2C) = low_cycles | (high_cycles << 16)
```

`oem_platform_init` calls `gpio_enable_alt(20)`, then `pwm_set(1000, 70)`, after `lcd_init` returns:

```
period_cycles = 12_000_000 / 1000 = 12000
high_cycles   = 70 * 12000 / 100  = 8400   = 0x20D0
low_cycles    = 30 * 12000 / 100  = 3600   = 0x0E10

SYSCTRL(0x2C) = 0x0E10 | (0x20D0 << 16) = 0x20D00E10
```

That gives a 1 kHz backlight PWM at 70% duty. A 100% duty encodes as `high = 0xFFFF, low = 0`, and a 0% duty as `high = 0, low = 0`.

### PWM Routing

The output of the PWM generator must reach a pad through the alt function mux. `oem_platform_init` does this with `gpio_enable_alt(20)`, where `20` is an **alt function ID**, not a physical pin number. The alt function behind ID `20` is probably the PWM pad routing to physical `GPIO1[9]`, which the bootrom GPIO crosswalk identifies as `WLED_PWM`. See the `Unresolved` section below for the caveat.

## Framebuffer Placement

EBOOT clears 5 MB at cached virtual `0x87B00000` and programs the LCD controller with the literal `0x07B00000`. On current hardware this configuration is the wrapped DDR framebuffer region that we usually call physical `0x33B00000`. That physical interpretation comes from the platform address-wrap behavior, not from `lcd_init` alone.

The pixel format is RGB565, 16 bpp. One line is therefore `800 * 2 = 1600` bytes, and the whole active framebuffer is `1600 * 480 = 768000` bytes.

This framebuffer location holds only for the boot path that runs EBOOT to completion and hands off to software that inherits the LCD controller state. Once the WinCE display driver takes over, it allocates its own framebuffer and writes a new value into `LCD+0x14`. [NK Display Driver](../nk/display-driver.md) documents the WinCE-side address model.

A stable framebuffer layout for Linux needs a real driver that owns the LCD controller and programs `+0x14` itself, rather than one that inherits whatever `lcd_init` or WinCE left behind.

## Unresolved

- The meaning of the bits in `+0x10`, `+0x40..+0x58`, `+0xA8..+0xC8` and `+0xE8`. We know only that these values work for the 800x480 panel at a 25.5 MHz pixel clock with a 248 MHz CPU. Nobody reverse-engineered the individual bit assignments.
- The routing of alt function ID `20`. It probably drives `GPIO1[9] = WLED_PWM`, but nothing confirms that. A confirmation needs a walk of the per-alt stub for that ID and a cross-reference of the sharepin bit against a pin mapping. See [gpio-driver.md](gpio-driver.md) for the alt-ID-to-physical-pin problem in general.
- The meaning of PAL IOCTL `0x010120EC` with payload `0x30` during the LCD clock setup. A conservative reading is that EBOOT takes two separate clock-related steps for the LCD bring-up: this PAL IOCTL, plus the controller-local divider write at `LCD+0xE8`. The split of responsibility between them is unconfirmed.
- Whether the LCD controller uses the high nibble of `+0x14` for anything outside the framebuffer address field.
- Stable stuck-pixel test patterns, blanking behavior and dynamic resolution changes. Nothing exercises them. This document describes steady-state operation only.
