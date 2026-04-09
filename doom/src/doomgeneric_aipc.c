/*
 * doomgeneric platform implementation for the AIPC netbook.
 *
 * Memory layout (set by boot.sh before EXECUTE):
 *   DDR_DOOM_BASE  doom.bin loaded here, executed here
 *   DDR_WAD_BASE   DOOM1.WAD uploaded separately by boot.sh
 *
 * Display: 800x480 RGB565, framebuffer at 0x33ed3c00
 * TODO: determine if additional LCD controller registers must be poked to
 *       activate the display before writing to the framebuffer.
 * TODO: implement timer using AIPC/AK7802 timer peripheral.
 * TODO: implement input (keyboard controller).
 */

#include <stdint.h>
#include <stddef.h>
#include "../doomgeneric/doomgeneric/doomgeneric.h"

/*
 * Timer (Timer2 as free-running clocksource, no IRQ)
 *
 * Base: 0x08000018 (from docs/nboot; DT reg 0x80000018 has wrong prefix).
 * Timer2 control register: base + 0x04.
 * Counter counts DOWN from loaded value to 0, then wraps.
 * Clock source: 12 MHz crystal (xin12m from device tree).
 *
 * Register bits (from Linux driver anyka-timer.c):
 *   [25:0]  count value (26-bit, ANYKA_TIMER_COUNT_MASK)
 *   [26]    ENABLE
 *   [27]    LOAD    (load count value and start)
 *   [28]    INT_CLEAR
 *   [29]    INT_STATUS
 */

#define TIMER2_REG      (*(volatile uint32_t *)0x0800001Cu)  /* base+0x04 */

#define TIMER_ENABLE    (1u << 26)
#define TIMER_LOAD      (1u << 27)
#define TIMER_INT_CLEAR (1u << 28)
#define TIMER_COUNT_MASK 0x3FFFFFFu   /* bits [25:0] */
#define TIMER_MAX_COUNT  0x3FFFFFFu   /* (1 << 26) - 1 */

#define TIMER_CLOCK_HZ  12000000u
#define TICKS_PER_MS    (TIMER_CLOCK_HZ / 1000u)  /* 12000 */

static uint64_t s_ticks;       /* accumulated ticks since timer_init */
static uint32_t s_last_raw;    /* last raw down-counter value */

static void timer_init(void)
{
    /* Mirror of anyka_timer_load(): clear IRQ, write count, load+enable. */
    TIMER2_REG = TIMER_INT_CLEAR;
    TIMER2_REG = TIMER_MAX_COUNT;
    TIMER2_REG = TIMER_MAX_COUNT | TIMER_LOAD | TIMER_ENABLE;
    s_last_raw = TIMER_MAX_COUNT;
    s_ticks    = 0;
}

static void timer_poll(void)
{
    uint32_t raw = TIMER2_REG & TIMER_COUNT_MASK;
    uint32_t delta;

    if (raw <= s_last_raw) {
        /* Normal: counter has moved down (or stayed). */
        delta = s_last_raw - raw;
    } else {
        /* Counter wrapped through zero: last_raw -> 0 -> MAX_COUNT -> raw. */
        delta = s_last_raw + (TIMER_MAX_COUNT - raw) + 1u;
    }
    s_ticks    += delta;
    s_last_raw  = raw;
}

/* Display */
#define FB_BASE     ((volatile uint16_t *)0x33ed3c00)
#define FB_WIDTH    800
#define FB_HEIGHT   480
#define FB_STRIDE   800   /* pixels per row (stride 1600 bytes / 2 bytes per pixel) */

/* DOOM framebuffer is 320x200; scale 2x to 640x400 and center on 800x480. */
#define DOOM_W      320
#define DOOM_H      200
#define SCALE       2
#define X_OFFSET    ((FB_WIDTH  - DOOM_W * SCALE) / 2)   /* 80 */
#define Y_OFFSET    ((FB_HEIGHT - DOOM_H * SCALE) / 2)   /* 40 */

void doom_main(void)
{
    /* The WAD is loaded at DDR_WAD_BASE by boot.sh before execution.
     * DG_Init() registers it via W_AddFile(). argv is otherwise unused. */
    char *argv[] = { "doom" };
    doomgeneric_Create(1, argv);

    while (1)
        doomgeneric_Tick();
}

void DG_Init(void)
{
    timer_init();
    /* TODO: initialize UART for debug output */
    /* TODO: initialize AIPC display controller / framebuffer */
    /* TODO: call W_AddFile() with DDR_WAD_BASE to load the WAD */
}

void DG_DrawFrame(void)
{
    /* DG_ScreenBuffer: 320x200 array of 0xAARRGGBB pixels (doomgeneric format).
     * Blit with 2x nearest-neighbor scaling, centered on the 800x480 display.
     * Output format: RGB565 (r5g6b5, little-endian). */
    for (int y = 0; y < DOOM_H; y++) {
        for (int x = 0; x < DOOM_W; x++) {
            uint32_t argb = DG_ScreenBuffer[y * DOOM_W + x];
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >>  8) & 0xFF;
            uint8_t b =  argb        & 0xFF;
            uint16_t rgb565 = ((uint16_t)(r >> 3) << 11)
                            | ((uint16_t)(g >> 2) <<  5)
                            |  (uint16_t)(b >> 3);

            int fb_x = X_OFFSET + x * SCALE;
            int fb_y = Y_OFFSET + y * SCALE;
            FB_BASE[ fb_y      * FB_STRIDE + fb_x    ] = rgb565;
            FB_BASE[ fb_y      * FB_STRIDE + fb_x + 1] = rgb565;
            FB_BASE[(fb_y + 1) * FB_STRIDE + fb_x    ] = rgb565;
            FB_BASE[(fb_y + 1) * FB_STRIDE + fb_x + 1] = rgb565;
        }
    }
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t start = DG_GetTicksMs();
    while (DG_GetTicksMs() - start < ms)
        ;
}

uint32_t DG_GetTicksMs(void)
{
    timer_poll();
    return (uint32_t)(s_ticks / TICKS_PER_MS);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    /* TODO: poll AIPC keyboard controller. */
    (void)pressed;
    (void)key;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    /* No window manager on bare metal. */
    (void)title;
}
