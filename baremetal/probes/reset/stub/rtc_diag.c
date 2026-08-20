#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define CLOCK_DIV    (SYSCTRL_BASE + 0x04u)
#define CLOCK_GATE   (SYSCTRL_BASE + 0x0Cu)
#define INT_STATEN   (SYSCTRL_BASE + 0x4Cu)
#define RTC_CONF     (SYSCTRL_BASE + 0x50u)
#define RTC_DATA     (SYSCTRL_BASE + 0x54u)

#define RTC_READY    (1u << 24)
#define RTC_READ     (3u << 17)

#define RESULT       0x32008000u
#define MAGIC        0x52544344u
#define POLL_LIMIT   10000000u

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)RESULT;
    uint32_t conf = REG32(RTC_CONF);
    uint32_t polls = 0;

    out[0] = MAGIC;
    out[1] = 0xFFFFFFFFu;
    out[2] = REG32(CLOCK_DIV);
    out[3] = REG32(CLOCK_GATE);
    out[4] = REG32(INT_STATEN);
    out[5] = conf;
    out[6] = REG32(RTC_DATA);

    REG32(RTC_CONF) = (conf & ~0x7FFFFu) | RTC_READ | (4u << 14);
    out[7] = REG32(RTC_CONF);
    out[8] = REG32(INT_STATEN);

    while (!(REG32(INT_STATEN) & RTC_READY) && polls < POLL_LIMIT)
        polls++;

    out[9] = polls;
    out[10] = REG32(INT_STATEN);
    out[11] = REG32(RTC_CONF);
    out[12] = REG32(RTC_DATA);
    out[13] = REG32(RTC_DATA) & 0x3FFFu;
    out[14] = (polls < POLL_LIMIT) ? 1u : 0u;

    REG32(RTC_CONF) = conf;
    out[15] = REG32(RTC_CONF);
    out[1] = 0;
}
