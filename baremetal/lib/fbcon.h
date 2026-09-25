/*
 * pongoOS - https://checkra.in
 *
 * Copyright (C) 2019-2023 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Changes:
 * - RGB565 only, inside one rectangle, in a text colour that the caller sets.
 * - No copy of the background. Instead of the upstream blend, an opaque
 *   console fills each cell with basecolor, and a transparent console draws
 *   only glyph pixels and never clears.
 * - No logo and no cache maintenance. The caller must pass an uncached
 *   mapping.
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RGB565_BLACK  0x0000u
#define RGB565_WHITE  0xFFFFu
#define RGB565_GREEN  0x07E0u
#define RGB565_RED    0xF800u
#define RGB565_CYAN   0x07FFu
#define RGB565_GRAY   0x8410u

void screen_init(volatile uint16_t *fb, uint32_t row_pixels, uint32_t x0,
                 uint32_t y0, uint32_t width, uint32_t height, uint8_t scale,
                 uint16_t basecolor, bool opaque);
void screen_set_color(uint16_t color);
void screen_putc(uint8_t c);
void screen_write(const char *str);
void screen_puts(const char *str);
void screen_mark_banner(void);
void screen_clear_all(void);
/* Row n below the banner. */
void screen_set_row(uint32_t n);
