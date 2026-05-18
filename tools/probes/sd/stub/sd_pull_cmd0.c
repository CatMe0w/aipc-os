/*
 * sd_pull_cmd0 - enable pullups on GPIO39/40, then try CMD0
 *
 * PUPD2 (SYSCTRL+0xA0): GPIO39=bits[15:14], GPIO40=bits[17:16]
 * On AK98, each pin has 2 bits: 00=pullup, 10=pullup/pulldown
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

#define PUPD1         SYSCTRL(0x9C)  /* bank0 GPIO[31:0] */
#define PUPD2         SYSCTRL(0xA0)  /* bank1 GPIO[63:32] */

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
    for (uint32_t i = 0; i < 16u; i++) OUT[i] = 0;

    OUT[0] = 0x50434D44u; /* "PCMD" = Pull-up CMD0 */
    OUT[1] = 1u;

    /* reset */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT; delay_long();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT; delay_long();

    /* sharepin */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /*
     * Enable pull-ups on CMD/CLK (GPIO39/40).
     * PUPD2 format: 2 bits per pin (nibbles).
     *   GPIO39 = bank1 pin7:  bits[15:14] of PUPD2
     *   GPIO40 = bank1 pin8:  bits[17:16] of PUPD2
     * We'll try 0x00 pattern per nibble (= pullup enabled per AK98 comments).
     */
    OUT[3] = PUPD2;  /* before */
    PUPD2 &= ~((3u << 14) | (3u << 16));  /* clear 2-bit fields -> 00 = pullup */
    OUT[4] = PUPD2;  /* after */

    /* Also try pullups on DATA pins GPIO30-37:
     *   GPIO30-31 = bank0 bits[30:31] -> PUPD1 nibles[15]
     *   GPIO32-37 = bank1 bits[0:5]  -> PUPD2 nibbles[0:2]
     */
    PUPD1 &= ~((3u << 0) | (3u << 2) | (3u << 4) | (3u << 6) | (3u << 8)
             | (3u << 10) | (3u << 12) | (3u << 14) | (3u << 16) | (3u << 18)
             | (3u << 20) | (3u << 22) | (3u << 24) | (3u << 26) | (3u << 28)
             | (3u << 30));
    OUT[5] = PUPD1;  /* bank0 after */
    OUT[6] = PUPD2;  /* bank1 after */

    /* MCI init */
    MCI_CLOCK = 0;          delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL; delay_long();
    uint32_t clk = MCI_CLOCK;
    clk |= MCI_CLK_EN; clk &= ~0xFFFFu; clk |= 240;
    MCI_CLOCK = clk;        delay_long();

    OUT[7] = MCI_CLOCK;
    OUT[8] = MCI_STA;
    (void)MCI_STA;

    /* CMD0 */
    MCI_CMD = 0;

    /* poll STA for changes */
    uint32_t sta0 = MCI_STA;
    delay();
    uint32_t sta1 = MCI_STA;
    delay();
    uint32_t sta2 = MCI_STA;
    delay_long();
    uint32_t sta3 = MCI_STA;

    OUT[9]  = sta0;
    OUT[10] = sta1;
    OUT[11] = sta2;
    OUT[12] = sta3;
    OUT[13] = MCI_CMD;
    OUT[14] = 0;
    OUT[15] = 0;
}
