/*
 * sd_cmd0_v4_probe - use SYSCTRL+0x0C as reset (clock-gate toggle)
 *
 * RESET_CTRL_REG is SYSCTRL+0x0C, same as CLOCK_CTRL.
 * Reset = disable clock gate (set bit2), wait, enable (clear bit2).
 * Skip SYSCTRL+0x10 entirely -- wrong register on AK7802.
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20020000u
#define MCI(off)      REG32(MCI_BASE + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_CMD       MCI(0x0C)
#define MCI_STA       MCI(0x34)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)

#define CLK_MMC_BIT   (1u << 2)

#define STA_CMD_SENT  (1u << 5)

#define WAIT_LIMIT    2000000u

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void delay_long(void)
{
    for (volatile uint32_t i = 0; i < 200000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static uint32_t wait_sta_any(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        if (MCI_STA & mask)
            return i + 1u;
    }
    return 0;
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x4D433476u; /* "MC4v" */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    OUT[3] = SYSCTRL(0x0C);  /* snapshot before reset */
    OUT[4] = MCI_STA;        /* status before reset */

    /*
     * MMC reset: SYSCTRL+0x0C is both CLOCK_CTRL and RESET_CTRL.
     * Set bit2 (disable clock), wait, clear bit2 (enable clock).
     */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT;   /* disable clock = reset assert */
    delay_long();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT;   /* enable clock  = reset release */
    delay_long();

    OUT[5] = SYSCTRL(0x0C);  /* after reset */
    OUT[6] = MCI_CLOCK;      /* MCI_CLOCK after reset */
    OUT[7] = MCI_STA;        /* status after reset -- should be cleared! */
    (void)MCI_STA;

    /* sharepin: ePIN_AS_SDMMC1 (DATA pins only, CMD/CLK fixed) */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /* AK98 init sequence */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();

    uint32_t clk = MCI_CLOCK;
    OUT[8] = clk;            /* clock after enable */

    clk |= MCI_CLK_EN;
    clk &= ~0xFFFFu;
    clk |= 240;
    MCI_CLOCK = clk;
    delay_long();

    OUT[9]  = MCI_CLOCK;     /* final clock */
    OUT[10] = MCI_STA;       /* status before CMD0 */
    (void)MCI_STA;

    /* CMD0 */
    MCI_CMD = 0;
    uint32_t wait_ticks = wait_sta_any(STA_CMD_SENT);

    OUT[11] = wait_ticks;    /* CMD_SENT wait ticks */
    OUT[12] = MCI_STA;       /* status after CMD0 */
    OUT[13] = 0;
}
