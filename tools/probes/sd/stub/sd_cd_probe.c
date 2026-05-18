/*
 * sd_cd_probe - card detect via GPIO13 (SD_CD#, active low)
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

    OUT[0] = 0x4D4D4344u; /* "MCD" = MMC Card Detect */
    OUT[1] = 1u;
    OUT[2] = 0;

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

    /* Read GPIO1 input: bit 13 = SD_CD# (0 = card present) */
    uint32_t gpio1_in = SYSCTRL(0xBC);
    uint32_t cd       = (gpio1_in >> 13) & 1u;

    OUT[3] = gpio1_in;
    OUT[4] = cd;
    OUT[5] = SYSCTRL(0x7C);  /* GPIO1 direction */
    OUT[6] = 0;
    OUT[7] = 0;
}
