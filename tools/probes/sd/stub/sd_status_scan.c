/*
 * sd_status_scan - write various CMD values, record ALL STA bits
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20020000u
#define MCI(off)      REG32(MCI_BASE + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_STA       MCI(0x34)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)

#define CLK_MMC_BIT   (1u << 2)

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

static void snapshot(uint32_t slot, uint32_t label, uint32_t cmd, uint32_t arg, uint32_t wait, uint32_t pre_sta, uint32_t post_sta)
{
    volatile uint32_t *p = OUT + 4u + slot * 6u;
    p[0] = label;
    p[1] = cmd;
    p[2] = arg;
    p[3] = wait;
    p[4] = pre_sta;
    p[5] = post_sta;
}

static void do_cmd(uint32_t slot, uint32_t label, uint32_t cmd, uint32_t arg)
{
    uint32_t pre_sta, post_sta, wait_ticks = 0;
    uint32_t i;

    pre_sta = MCI_STA;
    MCI_ARG = arg;
    MCI_CMD = cmd;

    for (i = 0; i < 500000u; i++) {
        post_sta = MCI_STA;
        if (post_sta != pre_sta)
            break;
    }
    if (i < 500000u) wait_ticks = i + 1u;

    delay();
    post_sta = MCI_STA;  /* final after delay */
    snapshot(slot, label, cmd, arg, wait_ticks, pre_sta, post_sta);
}

void stub_main(void)
{
    uint32_t slot = 0;

    for (uint32_t i = 0; i < 64u; i++) OUT[i] = 0;

    OUT[0] = 0x53545343u; /* "STSC" — STatus SCanner */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* reset via clock gate toggle */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT;
    delay_long();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT;
    delay_long();

    /* sharepin: ePIN_AS_SDMMC1 */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /* AK98 init */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();
    uint32_t clk = MCI_CLOCK;
    clk |= MCI_CLK_EN;
    clk &= ~0xFFFFu;
    clk |= 240;
    MCI_CLOCK = clk;
    delay_long();

    OUT[3] = MCI_CLOCK;

    /*
     * Test commands with different register values.
     * CMD0 = 0 (no response)
     * CMD8: RESP_EN|RESP_CRC|8 = 0x0148 on AK98 (with bit6)
     * CMD8: on AK7802 maybe bit6 is not writable, try 0x0108 (no RESP_EN)
     * CMD55: RESP_EN|RESP_CRC|55 = 0x0177 on AK98
     * 0xF81: all writable bits set (triggers CMD_ACTIVE)
     */

    do_cmd(slot++, 0x434D4438u, 0x00000108u, 0x000001AAu);  /* CMD8: CRC|idx=8, no resp_en */
    do_cmd(slot++, 0x434D3535u, 0x00000177u, 0x00000000u);   /* CMD55: CRC|resp|idx=55 */
    do_cmd(slot++, 0x46463030u, 0x000000F0u, 0x00000000u);   /* 0xF0: high bits of idx */
    do_cmd(slot++, 0x434D3431u, 0x00000029u, 0x40FF8000u);   /* CMD41: resp|idx=41 */
    do_cmd(slot++, 0x434D3438u, 0x00000148u, 0x000001AAu);   /* CMD8: with bit6 set (AK98 style) */
    do_cmd(slot++, 0x434D3030u, 0x00000000u, 0x00000000u);   /* CMD0: zero */
    do_cmd(slot++, 0x46463831u, 0x00000F81u, 0x00000000u);   /* 0xF81: all writable */
}
