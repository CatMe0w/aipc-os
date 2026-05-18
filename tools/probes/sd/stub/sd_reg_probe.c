/*
 * sd_reg_probe - try reading MCI_CLOCK register (first MCI access!)
 *
 * If this hangs, MCI controller is not clocked or wrong base address.
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20020000u
#define MCI(off)      REG32(MCI_BASE + (off))

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

    OUT[0] = 0x4D4D5247u; /* "MRG" = MMC ReGister */
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

    /* Snapshot SYSCTRL before MCI access */
    OUT[3] = SYSCTRL(0x0C);
    OUT[4] = SYSCTRL(0x10);
    OUT[5] = SYSCTRL(0x74);
    OUT[6] = SYSCTRL(0x78);

    /*
     * First MCI access: read CLOCK register.
     * If this line hangs, the MCI controller is unclocked/missing.
     */
    OUT[7] = MCI(0x04);     /* read MCI_CLOCK */
    OUT[8] = MCI(0x34);     /* read MCI_STATUS */
    OUT[9] = 0;

    /* Try writing clock: enable + divider */
    MCI(0x04) = (1u << 20) | (1u << 16) | 240;
    delay();

    /* Read back */
    OUT[10] = MCI(0x04);    /* read back MCI_CLOCK */
    OUT[11] = 0;
}
