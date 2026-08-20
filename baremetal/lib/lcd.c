/*
 * LCD bring-up, a copy of the EBOOT lcd_init. See docs/eboot/lcd-driver.md.
 *
 * Two things here are not obvious and must stay as they are. The control
 * register at +0x00 needs three separate writes, with the other register
 * writes in between. SYSCTRL+0x0C needs a read-modify-write, because a plain
 * clear also stops clocks that we still need.
 */

#include "lcd.h"
#include "mmu.h"

#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL(off)  REG32(0x08000000u + (off))
#define LCD(off)      REG32(0x20010000u + (off))

#define RAMCTRL_AHB_PRIORITY  0x2002D014u

#define SYSCTRL_CLK_LCD_EN_N  0x00000008u
#define SYSCTRL_LCD_RESET     0x00080000u

static void busy_wait(volatile uint32_t count)
{
    while (count-- != 0)
        ;
}

static void gpio_set_output(uint32_t pin, uint32_t value)
{
    uint32_t bank = (pin >> 5) & 3u;
    uint32_t bit = pin & 0x1Fu;
    uint32_t dir = 0x08000000u + 0x7Cu + 8u * bank;
    uint32_t out = 0x08000000u + 0x80u + 8u * bank;

    REG32(dir) &= ~(1u << bit);
    if (value)
        REG32(out) |= (1u << bit);
    else
        REG32(out) &= ~(1u << bit);
}

void lcd_fill(uint16_t color)
{
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)FB_ADDR;

    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; ++i)
        fb[i] = color;
}

void lcd_init(void)
{
    SYSCTRL(0x74) = 0x00000008u;
    SYSCTRL(0x78) = 0x564F0010u;

    /* Panel power rails and reset. */
    gpio_set_output(104, 1);
    gpio_set_output(69, 0);
    gpio_set_output(4, 0);

    uint32_t clk_gate = SYSCTRL(0x0C) & ~SYSCTRL_CLK_LCD_EN_N;
    SYSCTRL(0x0C) = clk_gate | SYSCTRL_LCD_RESET;
    SYSCTRL(0x0C) = clk_gate;

    /* Scanout DMA must outrank the ARM core on the AHB. If it does not, cached
     * DDR traffic starves the LCD FIFO, and the image goes black or shifts.
     * Drain the write buffer, because it can already be on here. */
    REG32(RAMCTRL_AHB_PRIORITY) &= ~0x0000007Fu;
    drain_write_buffer();

    lcd_fill(0x0000u);

    LCD(0x3C) = 0x00000000u;
    LCD(0xE8) = 0x00000111u;
    LCD(0x00) = 0x00000040u;        /* control phase 1 */

    LCD(0x10) = 0x00300006u;
    LCD(0x40) = 0x00080003u;
    LCD(0x44) = 0x00058320u;
    LCD(0x48) = 0x00050420u;
    LCD(0x4C) = 0x00000018u;
    LCD(0x50) = 0x00000001u;
    LCD(0x54) = 0x00F00000u;
    LCD(0x58) = 0x000001F9u;

    LCD(0x00) = 0x80A80050u;        /* control phase 2 */

    LCD(0xB0) = 0x000C81E0u;
    LCD(0x14) = FB_DMA_ADDR;
    LCD(0x18) = 0x032001E0u;
    LCD(0x1C) = 0x00000000u;
    LCD(0x20) = 0x00000000u;
    LCD(0xA8) = 0x00000000u;
    LCD(0xAC) = 0x000C81E0u;

    LCD(0x00) = 0x80A80058u;        /* control phase 3, starts DMA */

    LCD(0xC8) = 0x00000800u;
    LCD(0xB8) = 0x00000004u;

    /* Let the panel settle before the backlight turns on, or it flashes white. */
    busy_wait(1500000u);
    SYSCTRL(0x2C) = 0x20D00E10u;
}
