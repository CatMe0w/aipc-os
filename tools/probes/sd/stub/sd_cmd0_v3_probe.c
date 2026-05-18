/*
 * sd_cmd0_v3_probe - use ePIN_AS_SDMMC1 style sharepin (DATA pins, not MDAT2)
 *
 * On AK7802, CMD/CLK are fixed-function on GPIO39/40.
 * DATA[3:0] share pins with NAND on GPIO[30:33].
 *
 * Sharepin: CON1 bits[18:16]=0x7, CON2 bits[4:3]=2 (MMC instead of NFC).
 * Do NOT touch CON1 bit29 (MDAT2, wrong pins for AK7802).
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
#define STA_RESP_END  (1u << 4)

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

    OUT[0] = 0x4D433376u; /* "MC3v" = MMC CMD0 v3 */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* clock gate + reset */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();

    /*
     * Sharepin: ePIN_AS_SDMMC1 variant for AK7802.
     * DATA pins GPIO30-37: CON1 bits[18:16]=0x7, CON2 bits[4:3]=2 (MMC)
     * CMD/CLK pins GPIO39/40: fixed-function, no sharepin needed.
     * Important: do NOT set CON1 bit29 (routes wrong pins on AK7802).
     */
    SYSCTRL(0x78) &= ~(7u << 16);          /* CON1: allow DATA as MMC */
    SYSCTRL(0x78) |= (7u << 16);           /* CON1: set bits[18:16]=7 */
    SYSCTRL(0x74) &= ~(3u << 3);           /* CON2: clear GRP3 */
    SYSCTRL(0x74) |= (2u << 3);            /* CON2: GRP3 = MMC */
    delay();

    OUT[3] = SYSCTRL(0x74);                /* sharepin0 after */
    OUT[4] = SYSCTRL(0x78);                /* sharepin1 after */

    /* AK98 init sequence */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();

    uint32_t clk = MCI_CLOCK;
    OUT[5] = clk;

    clk |= MCI_CLK_EN;
    clk &= ~0xFFFFu;
    clk |= 240;
    MCI_CLOCK = clk;
    delay_long();

    OUT[6] = MCI_CLOCK;
    OUT[7] = MCI_STA;
    (void)MCI_STA;

    /* CMD0 */
    MCI_ARG = 0;
    MCI_CMD = 0;

    uint32_t wait_ticks = wait_sta_any(STA_CMD_SENT);

    OUT[8]  = wait_ticks;
    OUT[9]  = MCI_STA;
    OUT[10] = MCI_CLOCK;
    OUT[11] = 0;
}
