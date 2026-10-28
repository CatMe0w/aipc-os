#pragma once
#include <stdint.h>

/* 800x480 RGB565. The controller takes the framebuffer base with the high
 * nibble masked off. See docs/eboot/lcd-driver.md. */
#define FB_WIDTH    800u
#define FB_HEIGHT   480u
#define FB_ADDR     0x33B00000u
#define FB_DMA_ADDR (FB_ADDR & 0x0FFFFFFFu)
#define FB_BYTES    (FB_WIDTH * FB_HEIGHT * 2u)

/* The pixel clock is PLL1 / (2 * (divider + 1)). Divider 8 gives 25.7 Hz on
 * this panel, divider 2 gives 77.1 Hz and costs the core about 24 percent of
 * its DDR speed. See docs/soc/lcd.md. */
#define LCD_DIVIDER_DEFAULT  8u

void lcd_init(void);
void lcd_init_divider(uint32_t divider);

/* Shows the framebuffer at addr from the next frame on. Returns when that
 * frame starts, so the buffer shown before is free to draw into. */
void lcd_flip(uint32_t addr);
uint32_t lcd_shown(void);

/* Turns the backlight off, then stops scan out, which frees DDR bandwidth.
 * In the other order the panel shows white for a moment. */
void lcd_dark(void);
/* Starts scan out again, and turns the backlight on after a frame. */
void lcd_light(uint32_t percent);
void lcd_fill(uint16_t color);

/* Sets the backlight PWM duty, 0 to 100 percent, at 1 kHz. */
void lcd_backlight(uint32_t percent);
