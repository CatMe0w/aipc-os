/*
 * sd_clock_probe - verify MMC clock gate + reset (no MCI register access)
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

    OUT[0] = 0x4D4D434Bu; /* "MCK" = MMC clocK */
    OUT[1] = 1u;
    OUT[2] = 0;

    /* Snapshot before */
    OUT[3] = SYSCTRL(0x0C);
    OUT[4] = SYSCTRL(0x10);

    /* Enable MMC clock gate: clear bit 2 */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();

    /* Pulse MMC reset: set then clear bit 18 */
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();

    /* Snapshot after */
    OUT[5] = SYSCTRL(0x0C);
    OUT[6] = SYSCTRL(0x10);
    OUT[7] = 0;
}
