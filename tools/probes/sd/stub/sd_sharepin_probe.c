/*
 * sd_sharepin_probe - add sharepin config (still no MCI access)
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 32u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x4D4D5350u; /* "MSP" = MMC SharePin */
    OUT[1] = 1u;
    OUT[2] = 0;

    /* Snapshot before */
    OUT[3] = SYSCTRL(0x74);
    OUT[4] = SYSCTRL(0x78);

    /* clock gate + reset (same as sd_clock_probe) */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();

    /* sharepin: 4-bit SD (ePIN_AS_SDMMC2) */
    SYSCTRL(0x78) |=  (1u << 29);                    /* CON1: MDAT2 (CMD+CLK) */
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 5))    /* CON2: clear GRP4 */
                  |  (2u << 5);                      /* CON2: 4-bit MMC */
    delay();

    /* Snapshot after */
    OUT[5] = SYSCTRL(0x74);
    OUT[6] = SYSCTRL(0x78);
    OUT[7] = 0;
}
