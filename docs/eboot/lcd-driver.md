# LCD Driver

EBOOT brings up the on-chip LCD controller for the 800x480 RGB565 panel, clears a framebuffer and starts scan out. It leaves the controller running, so whatever boots next inherits a live display.

[docs/soc/lcd.md](../soc/lcd.md) describes the controller itself: the register model, the field meanings, the pixel clock formula and the panel timing.

The sequence comes from the `lcd_init` assembly. Where the code uses a read-modify-write, the cold-boot-equivalent final literal appears instead.

## Bring-Up Sequence

```c
// 1. Clock and reset pulse.
*LCD(0x3C) = 0;
*SYSCTRL(0x0C) |=  (1 << 19);    // assert LCD reset
*SYSCTRL(0x0C) &= ~(1 << 19);    // deassert (pulse)
*SYSCTRL(0x0C) &= ~(1 <<  3);    // enable LCD clock (inverted polarity)

// 2. Clear 5 MB of framebuffer memory.
memset(fb_virt, 0, 5 * 1024 * 1024);

// 3. Program the pixel clock and issue the PAL IOCTL.
*LCD(0xE8) = 0x00000111;
pal_ioctl(0x010120EC, &value_0x30, 4, 0, 0, 0);

// 4. Ensure the LCD pad routing is enabled.
//    hw_phase1_init already enables alt IDs 44, 8, 53, 13, 12, 16, 51, 52.
//    lcd_init itself re-enables only alt ID 51.
gpio_enable_alt(51);

// 5. Interface select.
*LCD(0x00) = 0x00000040;

// 6. Panel timing.
*LCD(0x10) = 0x00300006;
*LCD(0x40) = 0x00080003;
*LCD(0x44) = 0x00058320;
*LCD(0x48) = 0x00050420;
*LCD(0x4C) = 0x00000018;
*LCD(0x50) = 0x00000001;
*LCD(0x54) = 0x00F00000;
*LCD(0x58) = 0x000001F9;

// 7. FIFO thresholds and pixel clock polarity.
*LCD(0x00) = 0x80A80050;

// 8. Layout and framebuffer base.
*LCD(0xB0) = 0x000C81E0;
*LCD(0x14) = 0x07B00000;         // actual code first preserves the high nibble
*LCD(0x18) = 0x032001E0;
*LCD(0xA8) = 0;
*LCD(0xAC) = 0x000C81E0;

// 9. Enable the RGB layer.
*LCD(0x00) |= 0x08;              // final value 0x80A80058

// 10. Commit and start.
*LCD(0xC8) |= 0x800;
*LCD(0xB8) = (*LCD(0xB8) & ~1) | 4;
```

A port must keep this order. The critical points:

- `SYSCTRL+0x0C` bit 3 has inverted polarity. Clear it to enable.
- `SYSCTRL+0x0C` bit 19 is a pulse. Toggle it high, then low.
- `LCD+0x00` takes three writes, not one. Steps 5, 7 and 9 select the interface, then set the FIFO thresholds, then enable the layer. A single write of the final value does not work.
- The timing registers must go out between steps 5 and 7, and the layout registers between steps 7 and 9.
- `pal_ioctl(0x010120EC, &0x30, 4, 0, 0, 0)` comes before the first `LCD+0x00` write.

## EBOOT Leaves the Panel at 25.7 Hz

`LCD+0xE8 = 0x111` selects divider 8, which gives a 13.78 MHz pixel clock and a 25.7 Hz refresh rate on this panel.

The panel timing in the device firmware names a 25.5 MHz pixel clock. The nearest divider to it is 4, at 24.80 MHz and 46.2 Hz, and `0x109` selects it. See [docs/soc/lcd.md](../soc/lcd.md) for the formula and for what the higher rates cost in memory bandwidth.

## Framebuffer Placement

The framebuffer is at physical `0x33B00000`. EBOOT clears 5 MB at cached virtual `0x87B00000` and writes the literal `0x07B00000` into `LCD+0x14`. The controller takes bits 27:0 of that register as the scan out address, and the 64 MB of DDR repeats through that address range, so `0x07B00000` and `0x03B00000` both reach `0x33B00000`. The [LCD timing probe](../../baremetal/probes/lcd-timing/README.md) confirms it: with the MMU off, CPU writes to `0x33B00000` appear on the panel with `0x03B00000` in `LCD+0x14`.

Live pixels take `800 * 480 * 2 = 768000` bytes. The region rounds up to 5 MB, which reaches the top of DDR.

The placement lasts until NK starts. The WinCE display driver allocates its own framebuffer and writes a new base. See [NK Display Driver](../nk/display-driver.md).

## Backlight

The backlight is not part of `lcd_init`. `oem_platform_init` does it afterwards: `gpio_enable_alt(20)` to route the pad, then `pwm_set(1000, 70)` for 1 kHz at 70 percent duty. [docs/soc/lcd.md](../soc/lcd.md) has the PWM encoding.

## Unresolved

- The routing of alt function ID `20`. It probably drives `GPIO1[9] = WLED_PWM`, but nothing confirms that. A confirmation needs a walk of the per-alt stub for that ID and a cross-reference of the sharepin bit against a pin mapping. See [gpio-driver.md](gpio-driver.md) for the alt-ID-to-physical-pin problem in general.
- The meaning of PAL IOCTL `0x010120EC` with payload `0x30`. EBOOT takes two clock-related steps during LCD bring-up, this IOCTL and the divider write at `LCD+0xE8`. The split of responsibility between them is unconfirmed.
