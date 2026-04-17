/*
 * doomgeneric platform implementation for the AIPC netbook.
 *
 * Display: 800x480 RGB565
 *
 * For the current no-MMU/no-D-cache baseline, stick to the original EBOOT
 * framebuffer pair at 0x33B00000/0x07B00000 to keep the display path as
 * simple as possible while input support is being brought up.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../doomgeneric/doomgeneric/doomgeneric.h"

#define REG32(addr)  (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE  0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define LCD_BASE      0x20010000u
#define LCD(off)      REG32(LCD_BASE + (off))

/* Timing
 *
 * USB-boot probing showed that timer2 uses:
 *   SYSCTRL+0x1C   control / reload register
 *   SYSCTRL+0x104  live 26-bit down-counter
 *
 * Reading back SYSCTRL+0x1C returns the stored reload value, not the live
 * counter, so DOOM must integrate deltas from SYSCTRL+0x104 directly.
 *
 * The timer runs at ~12 MHz and auto-reloads, so the 26-bit live counter wraps
 * every ~5.59 s. Callers therefore need to poll more frequently than one wrap;
 * DOOM's main loop and sleep loop do. */

#define TIMER2_CTRL_OFFSET      0x1Cu
#define TIMER2_LIVE_OFFSET      0x104u
#define TIMER_COUNT_MASK        0x03FFFFFFu
#define TIMER_CTRL_ENABLE       0x04000000u
#define TIMER_TICKS_PER_MS      12000u
#define SYSCTRL_CLK_LCD_EN_N    0x00000008u
#define SYSCTRL_LCD_RESET       0x00080000u

static uint32_t s_timer_last_raw;
static uint64_t s_timer_total_ticks;

extern void aipc_mmu_cache_init(void);
extern void aipc_drain_write_buffer(void);

static inline uint32_t timer_read_live_raw(void)
{
    return SYSCTRL(TIMER2_LIVE_OFFSET) & TIMER_COUNT_MASK;
}

static void timer_arm_max_period(void)
{
    /* Program max reload, then enable timer2. */
    SYSCTRL(TIMER2_CTRL_OFFSET) = TIMER_COUNT_MASK;
    SYSCTRL(TIMER2_CTRL_OFFSET) = TIMER_COUNT_MASK | TIMER_CTRL_ENABLE;
}

static uint64_t timer_get_total_ticks(void)
{
    uint32_t raw = timer_read_live_raw();
    uint32_t elapsed = (s_timer_last_raw - raw) & TIMER_COUNT_MASK;

    s_timer_total_ticks += elapsed;
    s_timer_last_raw = raw;
    return s_timer_total_ticks;
}

static void timer_init(void)
{
    timer_arm_max_period();
    s_timer_last_raw = timer_read_live_raw();
    s_timer_total_ticks = 0;
}

void DG_SleepMs(uint32_t ms)
{
    uint64_t target = timer_get_total_ticks() + (uint64_t)ms * TIMER_TICKS_PER_MS;

    while (timer_get_total_ticks() < target)
        ;
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)(timer_get_total_ticks() / TIMER_TICKS_PER_MS);
}

/* Display */

#define FB_BOOT_VIRT        0x33B00000u
#define FB_BOOT_DMA_BASE    0x07B00000u
#define FB_RUNTIME_VIRT     0x33ED3C00u
#define FB_WIDTH    800
#define FB_HEIGHT   480
#define FB_STRIDE   800

/* Render DOOM at its native 320x200 and scale 2x on blit to the LCD. */
#define DG_SCALE    2
#define DG_SRC_W    DOOMGENERIC_RESX
#define DG_SRC_H    DOOMGENERIC_RESY
#define DG_OUT_W    (DG_SRC_W * DG_SCALE)
#define DG_OUT_H    (DG_SRC_H * DG_SCALE)
#define X_OFFSET    ((FB_WIDTH  - DG_OUT_W) / 2)   /* 80 */
#define Y_OFFSET    ((FB_HEIGHT - DG_OUT_H) / 2)   /* 40 */

static int s_first_frame_logged;
static volatile uint16_t *s_fb_base = (volatile uint16_t *)FB_BOOT_VIRT;

static void busy_wait(volatile int count)
{
    while (count-- > 0)
        ;
}

static uint32_t diag_hash32(const uint32_t *buf, size_t words)
{
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < words; i++) {
        hash ^= buf[i];
        hash *= 16777619u;
    }

    return hash;
}

static void log_lcd_state(const char *stage)
{
    printf("LCD state %s: ctrl=0x%08x fb=0x%08x stride=0x%08x size=0x%08x b8=0x%08x c8=0x%08x s3c=0x%08x\n",
           stage,
           (unsigned int)LCD(0x00),
           (unsigned int)LCD(0x14),
           (unsigned int)LCD(0xAC),
           (unsigned int)LCD(0x18),
           (unsigned int)LCD(0xB8),
           (unsigned int)LCD(0xC8),
           (unsigned int)LCD(0x3C));
}

static void gpio_set_output(int pin, int value)
{
    int bank = (pin >> 5) & 3;
    int bit  = pin & 0x1F;
    uint32_t dir_addr = SYSCTRL_BASE + 0x7C + 8 * bank;
    uint32_t out_addr = SYSCTRL_BASE + 0x80 + 8 * bank;
    REG32(dir_addr) &= ~(1u << bit);
    if (value)
        REG32(out_addr) |= (1u << bit);
    else
        REG32(out_addr) &= ~(1u << bit);
}

/*
 * lcd_init - bring up the AK7802 LCD controller from cold USB-boot state.
 *
 * Register values verified from EBOOT lcd_init ASSEMBLY (not decompiler
 * output - Hex-Rays produced wrong constants for 4 registers).
 * Validated via host-side usbboot poke on 2026-04-12.
 */
static void lcd_init(void)
{
    /* Enable all peripheral clocks.
     * In normal boot, nboot clears these. USB boot skips nboot. 
     * WARNING: Setting SYSCTRL(0x0C) = 0 actually shuts down the timer clock
     * because 0 doesn't enable everything correctly for timer!
     * We will preserve the bootrom's clock/IMR states. */
    // SYSCTRL(0x0C) = 0;
    // SYSCTRL(0x34) = 0;
    // SYSCTRL(0x38) = 0;

    /* Sharepin mux */
    SYSCTRL(0x74) = 0x00000008;
    SYSCTRL(0x78) = 0x564F0010;

    /* Panel power GPIOs */
    gpio_set_output(104, 1);
    gpio_set_output(69, 0);
    gpio_set_output(4, 0);

    /* Preserve bootrom clock gates; only enable LCD clock and pulse reset. */
    uint32_t clk_gate = SYSCTRL(0x0C) & ~SYSCTRL_CLK_LCD_EN_N;
    SYSCTRL(0x0C) = clk_gate | SYSCTRL_LCD_RESET;
    SYSCTRL(0x0C) = clk_gate;

    /* Clear both candidate framebuffers while we stay on the EBOOT base. */
    memset((void *)FB_BOOT_VIRT, 0, FB_WIDTH * FB_HEIGHT * 2);
    memset((void *)FB_RUNTIME_VIRT, 0, FB_WIDTH * FB_HEIGHT * 2);

    /* LCD controller registers */
    LCD(0x3C) = 0x00000000;
    LCD(0xE8) = 0x00000111;
    LCD(0x00) = 0x00000040;            /* phase 1 */

    LCD(0x10) = 0x00300006;
    LCD(0x40) = 0x00080003;
    LCD(0x44) = 0x00058320;
    LCD(0x48) = 0x00050420;
    LCD(0x4C) = 0x00000018;
    LCD(0x50) = 0x00000001;
    LCD(0x54) = 0x00F00000;
    LCD(0x58) = 0x000001F9;

    LCD(0x00) = 0x80A80050;            /* phase 2 */

    LCD(0xB0) = 0x000C81E0;
    LCD(0x14) = FB_BOOT_DMA_BASE;
    LCD(0x18) = 0x032001E0;
    LCD(0xA8) = 0x00000000;
    LCD(0xAC) = 0x000C81E0;

    LCD(0x00) = 0x80A80058;            /* phase 3 start DMA */

    LCD(0xC8) = 0x00000800;
    LCD(0xB8) = 0x00000004;

    /* avoid white screen flashes */
    busy_wait(1500000);

    /* PWM backlight */
    SYSCTRL(0x2C) = 0x20D00E10;
}

/* Entry point */

#ifndef WAD_FILENAME
#define WAD_FILENAME "doom1.wad"
#endif

void doom_main(void)
{
    char *argv[] = { "doom", "-iwad", WAD_FILENAME };
    doomgeneric_Create(3, argv);

    while (1)
        doomgeneric_Tick();
}

/* doomgeneric callbacks */

void DG_Init(void)
{
    lcd_init();
    /*
     * The current USB-boot LCD bring-up rewrites SYSCTRL clock/reset
     * registers early on. Arm the timer only after that sequence completes,
     * otherwise the counter state can be clobbered before DOOM starts using it.
     */
    timer_init();
    aipc_mmu_cache_init();
    s_fb_base = (volatile uint16_t *)FB_BOOT_VIRT;
    printf("DG_Init: framebuffer va=0x%08x dma=0x%08x (MMU off, D-cache off)\n",
           (unsigned int)(uintptr_t)s_fb_base,
           (unsigned int)FB_BOOT_DMA_BASE);
    log_lcd_state("after init");
}

void DG_DrawFrame(void)
{
    /* With write buffer enabled, drain stores before reading/writing buffers. */
    aipc_drain_write_buffer();

    if (!s_first_frame_logged) {
        s_first_frame_logged = 1;
        log_lcd_state("at first draw");
        printf("DG_DrawFrame diag: fb_base=0x%08x dg0=%08x dg1=%08x hash=0x%08x\n",
               (unsigned int)(uintptr_t)s_fb_base,
               (unsigned int)DG_ScreenBuffer[0],
               (unsigned int)DG_ScreenBuffer[1],
               (unsigned int)diag_hash32((const uint32_t *)DG_ScreenBuffer, 256));
    }

    /* DG_ScreenBuffer is 320x200 ARGB8888. Scale 2x to 640x400 RGB565. */
    for (int y = 0; y < DG_SRC_H; y++) {
        const uint32_t *src_row = DG_ScreenBuffer + y * DG_SRC_W;
        volatile uint16_t *dst_row0 =
            s_fb_base + (Y_OFFSET + y * DG_SCALE) * FB_STRIDE + X_OFFSET;
        volatile uint16_t *dst_row1 = dst_row0 + FB_STRIDE;

        for (int x = 0; x < DG_SRC_W; x++) {
            uint32_t argb = src_row[x];
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >>  8) & 0xFF;
            uint8_t b =  argb        & 0xFF;
            uint16_t rgb565 = ((uint16_t)(r >> 3) << 11)
                            | ((uint16_t)(g >> 2) <<  5)
                            |  (uint16_t)(b >> 3);
            int dst_x = x * DG_SCALE;

            dst_row0[dst_x] = rgb565;
            dst_row0[dst_x + 1] = rgb565;
            dst_row1[dst_x] = rgb565;
            dst_row1[dst_x + 1] = rgb565;
        }
    }

    aipc_drain_write_buffer();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    (void)pressed;
    (void)key;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}
