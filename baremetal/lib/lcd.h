#pragma once
#include <stdint.h>

/* 800x480 RGB565. The controller takes the framebuffer base with the high
 * nibble masked off. See docs/eboot/lcd-driver.md. */
#define FB_WIDTH    800u
#define FB_HEIGHT   480u
#define FB_ADDR     0x33B00000u
#define FB_DMA_ADDR (FB_ADDR & 0x0FFFFFFFu)
#define FB_BYTES    (FB_WIDTH * FB_HEIGHT * 2u)

void lcd_init(void);
void lcd_fill(uint16_t color);
