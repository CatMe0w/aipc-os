#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define INT_STATEN   (SYSCTRL_BASE + 0x4Cu)
#define RTC_CONF     (SYSCTRL_BASE + 0x50u)
#define RTC_DATA     (SYSCTRL_BASE + 0x54u)

#define RTC_READY    (1u << 24)
#define RTC_READ     (3u << 17)
#define RTC_WRITE    (2u << 17)

static void rtc_wait_ready(void)
{
    while (!(REG32(INT_STATEN) & RTC_READY))
        ;
}

static uint32_t rtc_read(uint32_t index)
{
    uint32_t conf = REG32(RTC_CONF) & ~0x7FFFFu;

    REG32(RTC_CONF) = conf | RTC_READ | (index << 14);
    rtc_wait_ready();
    return REG32(RTC_DATA) & 0x3FFFu;
}

static void rtc_write(uint32_t index, uint32_t value, int wait)
{
    uint32_t conf = REG32(RTC_CONF) & ~0x7FFFFu;

    REG32(RTC_CONF) = conf | RTC_WRITE | (index << 14) | (value & 0x3FFFu);
    if (wait)
        rtc_wait_ready();
}

void stub_main(void)
{
    (void)rtc_read(4);
    rtc_write(4, 0x29F8u, 1);
    (void)rtc_read(5);
    rtc_write(5, 0x2001u, 0);

    for (;;)
        ;
}
