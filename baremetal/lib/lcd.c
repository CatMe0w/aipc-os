#include "lcd.h"
#include "mmu.h"
#include "mmio.h"

#define SYSCTRL(off)  REG32(0x08000000u + (off))
#define LCD(off)      REG32(0x20010000u + (off))

#define RAMCTRL_AHB_PRIORITY  0x2002D014u

#define LCD_RGB_BASE          0x14u
#define LCD_OPERATION         0xB8u
#define  SCAN_OUT_STOP        0x00000001u
#define  SCAN_OUT_START       0x00000004u
#define LCD_STATUS            0xBCu
#define  STATUS_REFRESH_START 0x00000010u
#define LCD_SOFT_CTRL         0xC8u
#define  SOFT_CTRL_COMMIT     0x00000800u

/* Longer than one frame at the slowest pixel clock */
#define FRAME_SPIN_LIMIT      20000000u

#define SYSCTRL_CLK_LCD_EN_N  0x00000008u
#define SYSCTRL_LCD_RESET     0x00080000u

static uint32_t shown = FB_ADDR;

static void busy_wait(volatile uint32_t count)
{
    while (count-- != 0)
        ;
}

void lcd_fill(uint16_t color)
{
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)FB_ADDR;

    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; ++i)
        fb[i] = color;
}

void lcd_init(void)
{
    lcd_init_divider(LCD_DIVIDER_DEFAULT);
}

void lcd_init_divider(uint32_t divider)
{
    SYSCTRL(0x74) = 0x00000008u;
    SYSCTRL(0x78) = 0x564F0010u;

    /* Read-modify-write: a plain clear also stops clocks that are in use. */
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
    /* Bit 8 enables the clock, bit 0 marks the divider in bits 7:1 valid. */
    LCD(0xE8) = 0x00000101u | (divider & 0x7Fu) << 1;
    /* Three separate writes to +0x00, with the other writes between them */
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
    shown = FB_ADDR;
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
    lcd_backlight(100u);
}

/* The PWM counts a fixed 12 MHz: bits 31:16 are the high time, bits 15:0 the
 * low time. See docs/soc/lcd.md. */
#define BACKLIGHT_PERIOD  12000u  /* 1 kHz */

void lcd_backlight(uint32_t percent)
{
    uint32_t high;

    /* A register of zero left the backlight on at a restart from full
     * duty. A high time of zero and a full low time keep the output low. */
    if (percent == 0) {
        SYSCTRL(0x2C) = BACKLIGHT_PERIOD;
        return;
    }
    if (percent >= 100u) {
        SYSCTRL(0x2C) = 0xFFFF0000u;
        return;
    }
    high = BACKLIGHT_PERIOD * percent / 100u;
    SYSCTRL(0x2C) = (high << 16) | (BACKLIGHT_PERIOD - high);
}

/* The status bits clear on read, so the first read drops an old event. */
static void wait_refresh_start(void)
{
    (void)LCD(LCD_STATUS);
    for (uint32_t i = 0; i < FRAME_SPIN_LIMIT; i++)
        if (LCD(LCD_STATUS) & STATUS_REFRESH_START)
            return;
}

void lcd_flip(uint32_t addr)
{
    LCD(LCD_RGB_BASE) = addr & 0x0FFFFFFFu;
    LCD(LCD_SOFT_CTRL) = SOFT_CTRL_COMMIT;
    wait_refresh_start();
    shown = addr;
}

uint32_t lcd_shown(void)
{
    return shown;
}

/* A new duty can wait for the end of the current PWM period, and a full
 * duty period is 0xFFFF counts of 12 MHz, about 5.5 ms. */
#define BACKLIGHT_SETTLE  600000u  /* busy_wait() loops, well over that */

void lcd_dark(void)
{
    lcd_backlight(0);
    busy_wait(BACKLIGHT_SETTLE);
    LCD(LCD_OPERATION) = SCAN_OUT_STOP;
}

void lcd_light(uint32_t percent)
{
    LCD(LCD_OPERATION) = SCAN_OUT_START;
    wait_refresh_start();
    wait_refresh_start();
    lcd_backlight(percent);
}
