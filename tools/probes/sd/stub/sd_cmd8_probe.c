/* sd_cmd8_probe - just CMD8 (0x108), no response expected, watch STA */
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

void stub_main(void)
{
    uint32_t sta_samples[8];
    uint32_t n = 0;

    for (uint32_t i = 0; i < 64u; i++) OUT[i] = 0;

    OUT[0] = 0x4D433844u; /* "MC8D" = MMC CMD8 Data */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* reset via clock toggle */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT; delay_long();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT; delay_long();

    /* sharepin */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /* init */
    MCI_CLOCK = 0;          delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL; delay_long();
    uint32_t clk = MCI_CLOCK;
    clk |= MCI_CLK_EN; clk &= ~0xFFFFu; clk |= 240;
    MCI_CLOCK = clk;        delay_long();

    OUT[3] = MCI_CLOCK;
    (void)MCI_STA;

    /* sample STA before */
    OUT[4] = MCI_STA;

    /* CMD8: SEND_IF_COND with 0x1AA, CRC check, index=8, no RESP_EN */
    MCI_ARG = 0x000001AAu;
    MCI_CMD = 0x00000108u;

    /* sample STA repeatedly after CMD */
    for (n = 0; n < 8; n++) {
        delay();  /* ~20k cycles between samples */
        sta_samples[n] = MCI_STA;
    }

    OUT[5] = sta_samples[0];
    OUT[6] = sta_samples[1];
    OUT[7] = sta_samples[2];
    OUT[8] = sta_samples[3];
    OUT[9] = sta_samples[4];
    OUT[10]= sta_samples[5];
    OUT[11]= sta_samples[6];
    OUT[12]= sta_samples[7];
    OUT[13]= MCI_RESP0;
    OUT[14]= MCI_CMD;       /* did CMD register change? */
    OUT[15]= SYSCTRL(0x0C);
}
