#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off) REG32(SYSCTRL_BASE + (off))

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

#define GPIO84_BIT (1u << 20)
#define GPIO84_SHAREPIN_BIT (1u << 8)

enum {
    STAGE_BASELINE = 0,
    STAGE_GPIO_MUX = 1,
    STAGE_GPIO_HIGH = 2,
    STAGE_GPIO_LOW = 3,
    STAGE_GPIO_HIGH2 = 4,
};

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 128u; i++)
        OUT[i] = 0;
}

static void record(uint32_t slot, uint32_t stage)
{
    volatile uint32_t *p = OUT + 4u + slot * 10u;

    p[0] = stage;
    p[1] = SYSCTRL(0x0C);
    p[2] = SYSCTRL(0x74);
    p[3] = SYSCTRL(0x78);
    p[4] = SYSCTRL(0x8C);
    p[5] = SYSCTRL(0x90);
    p[6] = SYSCTRL(0xC4);
    p[7] = SYSCTRL(0x94);
    p[8] = SYSCTRL(0x98);
    p[9] = SYSCTRL(0xC8);
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x47383450u; /* "G84P" */
    OUT[1] = 1u;
    OUT[2] = GPIO84_BIT;
    OUT[3] = GPIO84_SHAREPIN_BIT;

    record(0, STAGE_BASELINE);

    SYSCTRL(0x74) &= ~GPIO84_SHAREPIN_BIT;
    delay();
    record(1, STAGE_GPIO_MUX);

    SYSCTRL(0x8C) &= ~GPIO84_BIT;
    SYSCTRL(0x90) |= GPIO84_BIT;
    delay();
    record(2, STAGE_GPIO_HIGH);

    SYSCTRL(0x90) &= ~GPIO84_BIT;
    delay();
    record(3, STAGE_GPIO_LOW);

    SYSCTRL(0x90) |= GPIO84_BIT;
    delay();
    record(4, STAGE_GPIO_HIGH2);
}
