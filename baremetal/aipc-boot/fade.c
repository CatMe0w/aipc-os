#include <string.h>
#include "fade.h"
#include "lcd.h"

#define FADE_STEPS   16

/* A cached copy of the screen. The framebuffers are uncached and slow to
 * read. */
static uint32_t frame[FB_BYTES / 4];

/* Scales one RGB565 pixel by a / 32. The spread form keeps a gap above each
 * channel, so one multiply scales all three. */
static uint32_t dim(uint32_t p, uint32_t a)
{
    uint32_t x = (p | (p << 16)) & 0x07E0F81Fu;

    x = ((x * a) >> 5) & 0x07E0F81Fu;
    return (x | (x >> 16)) & 0xFFFFu;
}

void fade_out(void)
{
    /* Each step draws into the buffer that is not shown, then flips to it.
     * The step count makes the last step land on FB_ADDR. */
    uint32_t from = lcd_shown();
    uint32_t steps = from == FB_ADDR ? FADE_STEPS : FADE_STEPS - 1;

    memcpy(frame, (const void *)(uintptr_t)from, FB_BYTES);
    for (uint32_t step = 1; step <= steps; step++) {
        uint32_t base = (steps - step) % 2 ? FB_BACK_ADDR : FB_ADDR;
        volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)base;
        uint32_t a = 32u * (steps - step) / steps;

        for (uint32_t i = 0; i < FB_BYTES / 4; i++) {
            uint32_t w = frame[i];

            fb[i] = dim(w & 0xFFFFu, a) | dim(w >> 16, a) << 16;
        }
        lcd_flip(base);
    }
}
