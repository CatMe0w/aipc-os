/* sd_cmd_trace_probe - trace CMD + STA after writing CMD0 */
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

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x4D435452u; /* "MCTR" = MMC CMD Trace */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* clock gate + reset */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();

    /* sharepin: ePIN_AS_SDMMC1 variant */
    SYSCTRL(0x78) &= ~(1u << 29);           /* clear MDAT2 (wrong pins) */
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /* AK98 init sequence */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 240;
    delay_long();

    OUT[3] = MCI_CLOCK;
    OUT[4] = MCI_STA;
    (void)MCI_STA;

    /* snapshot before CMD0 */
    OUT[5] = MCI_CMD;
    OUT[6] = MCI_ARG;
    OUT[7] = MCI_STA;

    /* CMD0 */
    MCI_ARG = 0;
    MCI_CMD = 0;

    /* snapshot after CMD0 (immediately) */
    OUT[8]  = MCI_CMD;
    OUT[9]  = MCI_ARG;
    OUT[10] = MCI_STA;

    delay();
    OUT[11] = MCI_STA;       /* status after delay */
    delay_long();
    OUT[12] = MCI_STA;       /* status after longer delay */

    /* try CMD0 with all writable bits set */
    MCI_CMD = 0x0F81u;       /* every writable bit in command reg */
    delay();
    OUT[13] = MCI_CMD;
    OUT[14] = MCI_STA;

    delay_long();
    OUT[15] = MCI_STA;
}
