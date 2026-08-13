#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define GPIO4_DIR    (SYSCTRL_BASE + 0x94u)
#define GPIO4_OUT    (SYSCTRL_BASE + 0x98u)
#define GPIO4_IN     (SYSCTRL_BASE + 0xC8u)

#define POWER_ON_OUT (1u << 9)
#define POWER_ON_IN  (1u << 6)

#define RESULT       0x32008000u
#define MAGIC        0x50575230u
#define DELAY_COUNT  10000000u

static void delay(void)
{
    volatile uint32_t count = DELAY_COUNT;

    while (count--)
        ;
}

static uint32_t power_on_level(void)
{
    return (REG32(GPIO4_IN) & POWER_ON_IN) ? 1u : 0u;
}

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)RESULT;

    out[0] = MAGIC;
    out[1] = 1u;
    out[2] = REG32(GPIO4_DIR);
    out[3] = REG32(GPIO4_OUT);
    out[4] = REG32(GPIO4_IN);
    out[5] = power_on_level();

    REG32(GPIO4_OUT) |= POWER_ON_OUT;
    out[6] = REG32(GPIO4_OUT);
    REG32(GPIO4_DIR) &= ~POWER_ON_OUT;
    out[7] = REG32(GPIO4_DIR);
    out[8] = power_on_level();
    delay();
    out[9] = power_on_level();

    out[1] = 2u;
    REG32(GPIO4_OUT) &= ~POWER_ON_OUT;
    out[10] = REG32(GPIO4_OUT);
    out[11] = power_on_level();
    delay();
    out[12] = REG32(GPIO4_DIR);
    out[13] = REG32(GPIO4_OUT);
    out[14] = REG32(GPIO4_IN);
    out[15] = power_on_level();
    out[1] = 0u;
}
