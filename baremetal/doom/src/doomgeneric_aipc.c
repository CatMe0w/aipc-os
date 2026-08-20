/*
 * doomgeneric platform layer for the AIPC netbook. The LCD is 800x480
 * RGB565.
 *
 * This keeps the framebuffer that EBOOT uses, 0x33B00000 for the core and
 * 0x07B00000 for the LCD DMA. map_memory() keeps that section uncached, and
 * the rest of DDR can use the D-cache.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../doomgeneric/doomgeneric/doomgeneric.h"
#include "ch374_keyboard.h"
#include "lcd.h"
#include "log.h"
#include "mmu.h"
#include "soc.h"
#include "timer.h"

#define REG32(addr)  (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE  0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

/* Display. Geometry and the boot framebuffer come from lib/lcd.h. */

#define FB_BOOT_VIRT        FB_ADDR
#define FB_RUNTIME_VIRT     0x33ED3C00u
#define FB_STRIDE           FB_WIDTH

#define DDR_BASE      0x30000000u
#define DDR_SIZE      0x04000000u
#define PERIPH_BASE   0x20000000u   /* LCD, L2 control, RAMCTRL */
#define L2_BUF_BASE   0x48000000u

/*
 * The scanout framebuffer stays uncached, because the LCD controller does not
 * snoop the ARM926 D-cache. A section is 1 MB, and the 800x480x16bpp buffer
 * fits inside 0x33B00000..0x33BFFFFF.
 *
 * The L2 buffer needs a mapping even though DOOM never reads it. The UART
 * transmit port is at 0x48001000, and the shared drivers log through it.
 */
static void map_memory(void)
{
    mmu_reset();

    mmu_map(DDR_BASE, DDR_SIZE, MMU_CACHED);
    mmu_map(FB_BOOT_VIRT, MMU_SECTION_SIZE, MMU_DEVICE);

    /* Buffered, not cached, so the log is readable after a hang without a
     * cache clean. DOOM loads above this section and never writes to it. */
    mmu_map(LOG_BASE, MMU_SECTION_SIZE, MMU_BUFFERED);

    mmu_map(SYSCTRL_BASE, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(PERIPH_BASE, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(L2_BUF_BASE, MMU_SECTION_SIZE, MMU_DEVICE);

    mmu_start();
}

/* Timer2 is in lib/timer.c, shared with the other bare-metal images. */
void DG_SleepMs(uint32_t ms)
{
    timer_delay_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return timer_ms();
}

/* DOOM renders at its native 320x200. The blit scales it 2x onto the LCD. */
#define DG_SCALE    2
#define DG_SRC_W    DOOMGENERIC_RESX
#define DG_SRC_H    DOOMGENERIC_RESY
#define DG_OUT_W    (DG_SRC_W * DG_SCALE)
#define DG_OUT_H    (DG_SRC_H * DG_SCALE)
#define X_OFFSET    ((FB_WIDTH  - DG_OUT_W) / 2)   /* 80 */
#define Y_OFFSET    ((FB_HEIGHT - DG_OUT_H) / 2)   /* 40 */

static volatile uint16_t *s_fb_base = (volatile uint16_t *)FB_BOOT_VIRT;

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
    uart_init();
    log_init();
    log_puts("doom\n");

    lcd_init();

    /* lcd_init() clears the framebuffer that it scans out. Clear the WinCE
     * runtime one too, so no stale image shows if the LCD reads it. */
    memset((void *)FB_RUNTIME_VIRT, 0, FB_BYTES);

    /* Start the timer only after lcd_init(). The USB-boot LCD bring-up
     * rewrites SYSCTRL clock and reset registers, which destroys the counter
     * state. */
    timer_init();
    map_memory();
    s_fb_base = (volatile uint16_t *)FB_BOOT_VIRT;
    memset((void *)(uintptr_t)s_fb_base, 0, FB_WIDTH * FB_HEIGHT * 2);
    printf("DG_Init: framebuffer va=0x%08x dma=0x%08x\n",
           (unsigned int)(uintptr_t)s_fb_base,
           (unsigned int)FB_DMA_ADDR);
    aipc_keyboard_init();
}

void DG_DrawFrame(void)
{
    /* The write buffer is on, so drain the pending stores first. */
    drain_write_buffer();

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

    drain_write_buffer();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    return aipc_keyboard_get_event(pressed, key);
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}
