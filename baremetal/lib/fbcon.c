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
 * Changes: see fbcon.h.
 */

#include "fbcon.h"
#include "font8x8_basic.h"

static volatile uint16_t *gFramebuffer;
static uint32_t gWidth;
static uint32_t gHeight;
static uint32_t gRowPixels;
static uint32_t y_cursor;
static uint32_t x_cursor;
static uint8_t scale_factor;
static uint32_t bannerHeight;
static uint16_t basecolor;
static uint16_t fgcolor;
static bool opaque;

#define SCALE_FACTOR scale_factor
#define LEFT_MARGIN (4 * scale_factor)
#define ROW_HEIGHT (1 + 8 * SCALE_FACTOR)

static void screen_fill_rows(uint32_t from, uint32_t to)
{
    if (!opaque)
        return;
    for (uint32_t y = from; y < to && y < gHeight; y++) {
        for (uint32_t x = 0; x < gWidth; x++)
            gFramebuffer[x + y * gRowPixels] = basecolor;
    }
}

void screen_clear_all(void)
{
    y_cursor = bannerHeight;
    x_cursor = LEFT_MARGIN;
    screen_fill_rows(bannerHeight, gHeight);
}

static void screen_clear_row(void)
{
    screen_fill_rows(y_cursor, y_cursor + ROW_HEIGHT);
}

void screen_putc(uint8_t c)
{
    if (!gFramebuffer)
        return;
    if (c == '\b') {
        if (x_cursor > 8 * SCALE_FACTOR)
            x_cursor -= 8 * SCALE_FACTOR;
        else
            x_cursor = 0;
        if (LEFT_MARGIN > x_cursor)
            x_cursor = LEFT_MARGIN;
        return;
    }
    if (c == '\n' || (x_cursor + (8 * SCALE_FACTOR)) > (gWidth - LEFT_MARGIN)) {
        if ((y_cursor + 2 * ROW_HEIGHT) > gHeight)
            y_cursor = bannerHeight;
        else
            y_cursor += ROW_HEIGHT;
        x_cursor = LEFT_MARGIN;
        screen_clear_row();
    }
    if (c == '\n')
        return;
    if (c == '\r') {
        x_cursor = LEFT_MARGIN;
        screen_clear_row();
        return;
    }
    for (uint32_t x = 0; x < (8u * SCALE_FACTOR); x++) {
        for (uint32_t y = 0; y < (8u * SCALE_FACTOR); y++) {
            uint32_t ind = (x + x_cursor) + ((y + y_cursor) * gRowPixels);

            if (font8x8_basic[c & 0x7f][y / SCALE_FACTOR] & (1 << (x / SCALE_FACTOR)))
                gFramebuffer[ind] = fgcolor;
            else if (opaque)
                gFramebuffer[ind] = basecolor;
        }
    }
    x_cursor += 8 * SCALE_FACTOR;
}

void screen_write(const char *str)
{
    while (*str)
        screen_putc(*str++);
}

void screen_puts(const char *str)
{
    screen_write(str);
    screen_putc('\n');
}

void screen_mark_banner(void)
{
    bannerHeight = y_cursor;
}

void screen_set_color(uint16_t color)
{
    fgcolor = color;
}

void screen_set_row(uint32_t n)
{
    y_cursor = bannerHeight + n * ROW_HEIGHT;
    x_cursor = LEFT_MARGIN;
    screen_clear_row();
}

void screen_init(volatile uint16_t *fb, uint32_t row_pixels, uint32_t x0,
                 uint32_t y0, uint32_t width, uint32_t height, uint8_t scale,
                 uint16_t base, bool is_opaque)
{
    opaque = is_opaque;
    gFramebuffer = fb + x0 + y0 * row_pixels;
    gRowPixels = row_pixels;
    gWidth = width;
    gHeight = height;
    scale_factor = scale;
    basecolor = base;
    fgcolor = (uint16_t)~base;
    bannerHeight = 0;
    screen_clear_all();
    y_cursor = 0;
}
