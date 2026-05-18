/*
 * sd_sdio_probe - try SDIO controller at 0x20021000
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20021000u
#define MCI(off)      REG32(MCI_BASE + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_STA       MCI(0x34)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)
#define CLK_MMC_BIT   (1u << 2)
#define CLK_SDIO_BIT  (1u << 8)

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay_long(void)
{
    for (volatile uint32_t i = 0; i < 200000u; i++)
        __asm__ volatile ("" : : : "memory");
}

void stub_main(void)
{
    for (uint32_t i = 0; i < 16u; i++) OUT[i] = 0;
    OUT[0] = 0x5344494Fu; /* "SDIO" */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* Reset both clocks */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT | CLK_SDIO_BIT; delay_long();
    SYSCTRL(0x0C) &= ~(CLK_MMC_BIT | CLK_SDIO_BIT); delay_long();

    OUT[3] = SYSCTRL(0x0C);

    /* Read initial SDIO registers */
    OUT[4] = MCI_CLOCK;
    OUT[5] = MCI_STA;

    /* MCI init sequence */
    MCI_CLOCK = 0;          delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL; delay_long();
    uint32_t clk = MCI_CLOCK;
    OUT[6] = clk;

    clk |= MCI_CLK_EN; clk &= ~0xFFFFu; clk |= 240;
    MCI_CLOCK = clk;        delay_long();

    OUT[7] = MCI_CLOCK;
    OUT[8] = MCI_STA;
    (void)MCI_STA;

    /* CMD0 */
    MCI_CMD = 0;
    delay_long();
    OUT[9]  = MCI_STA;
    OUT[10] = MCI_CMD;

    /* CMD8 */
    MCI_ARG = 0x000001AAu;
    MCI_CMD = 0x00000108u;
    delay_long();
    OUT[11] = MCI_CMD;
    OUT[12] = MCI_STA;
    delay_long();
    OUT[13] = MCI_STA;
    delay_long();
    OUT[14] = MCI_STA;
    OUT[15] = MCI_BASE;
}
