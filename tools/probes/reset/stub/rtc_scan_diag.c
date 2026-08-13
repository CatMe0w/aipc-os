#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define INT_STATEN   (SYSCTRL_BASE + 0x4Cu)
#define RTC_CONF     (SYSCTRL_BASE + 0x50u)
#define RTC_DATA     (SYSCTRL_BASE + 0x54u)

#define RTC_READY    (1u << 24)
#define RTC_READ     (3u << 17)

#define RESULT       0x32008000u
#define MAGIC        0x52545343u
#define POLL_LIMIT   2000000u

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)RESULT;
    uint32_t original = REG32(RTC_CONF);
    uint32_t index;

    out[0] = MAGIC;
    out[1] = 0xFFFFFFFFu;
    out[2] = REG32(INT_STATEN);
    out[3] = original;

    for (index = 0; index < 6; index++) {
        uint32_t base = 4u + index * 5u;
        uint32_t polls = 0;
        uint32_t recover = 0;

        REG32(RTC_CONF) = (original & ~0x7FFFFu) |
                          RTC_READ | (index << 14);
        while (!(REG32(INT_STATEN) & RTC_READY) && polls < POLL_LIMIT)
            polls++;

        out[base + 0] = polls;
        out[base + 1] = REG32(INT_STATEN);
        out[base + 2] = REG32(RTC_DATA);

        REG32(RTC_CONF) = original;
        while (!(REG32(INT_STATEN) & RTC_READY) && recover < POLL_LIMIT)
            recover++;

        out[base + 3] = recover;
        out[base + 4] = REG32(INT_STATEN);
    }

    out[34] = REG32(RTC_CONF);
    out[1] = 0;
}
