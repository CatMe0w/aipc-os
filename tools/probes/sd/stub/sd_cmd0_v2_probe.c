/*
 * sd_cmd0_v2_probe - try proper AK98-style MCI init sequence
 *
 * 1. MCI_CLOCK = 0
 * 2. delay 1ms
 * 3. MCI_CLOCK = MCI_ENABLE(bit20) | FAIL_TRIGGER(bit19)
 * 4. Read back, OR in CLK_ENABLE(bit16) | PWRSAVE(bit17) | divider
 * 5. Send CMD0
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
#define MCI_RESP0     MCI(0x14)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)

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

    OUT[0] = 0x4D433076u; /* "MCv" = MMC CMD0 v2 */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* clock gate + reset + sharepin */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();
    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 5)) | (2u << 5);
    delay();

    OUT[3] = MCI_CLOCK;       /* bootrom default */

    /* AK98 init sequence: clear, then enable */
    MCI_CLOCK = 0;
    delay_long();              /* ~1ms at 200k iterations */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();

    OUT[4] = MCI_CLOCK;       /* after enable (should have bits 20,19) */

    /* Read back and add clock divider */
    uint32_t clk = MCI_CLOCK;
    OUT[5] = clk;              /* snapshot before modify */

    clk |= MCI_CLK_EN;        /* add CLK_ENABLE */
    clk &= ~0xFFFFu;          /* clear divider */
    clk |= 240;               /* divider = 240 */
    MCI_CLOCK = clk;
    delay_long();

    OUT[6] = MCI_CLOCK;       /* final clock config */
    OUT[7] = MCI_STA;         /* status before CMD0 */
    (void)MCI_STA;            /* clear stale */

    /* CMD0: GO_IDLE_STATE */
    MCI_ARG = 0;
    MCI_CMD = 0;

    uint32_t wait_ticks = wait_sta_any(STA_CMD_SENT);

    OUT[8]  = wait_ticks;     /* iterations waited */
    OUT[9]  = MCI_STA;        /* status after CMD0 */
    OUT[10] = MCI_CLOCK;      /* clock after CMD0 */
    OUT[11] = SYSCTRL(0x0C); /* clk_ctrl */
    OUT[12] = SYSCTRL(0x10); /* clk_con2 */
    OUT[13] = SYSCTRL(0x74); /* sharepin0 */
    OUT[14] = SYSCTRL(0x78); /* sharepin1 */
    OUT[15] = 0;
}
