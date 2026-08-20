/*
 * Write-readback round trip on the alarm register (window 3) to prove the
 * sideband actually moves data. Also reads the USB analog windows (4, 5).
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define SYSCTRL         0x08000000u
#define INT_STATEN      (SYSCTRL + 0x4Cu)
#define RTC_CONF        (SYSCTRL + 0x50u)
#define RTC_DATA        (SYSCTRL + 0x54u)

#define RTC_RDY         (1u << 24)
#define DIR_READ        (1u << 17)
#define START           ((1u << 21) | (1u << 19))
#define RTC_EN          (1u << 24)      /* RTC domain power */

#define WIN_ALARM1      3u
#define WIN_USB0        4u
#define WIN_USB1        5u

#define RESULT          0x48000600u
#define MAGIC           0x55534234u
#define STATUS_RUNNING  0xDEADu
#define POLL_LIMIT      0x10000u

#define XFER_WORDS      7u

static uint32_t g_conf_base;

static void sideband_xfer(uint32_t window, uint32_t value, uint32_t is_write,
                          volatile uint32_t *out)
{
    uint32_t n = 0;

    REG32(RTC_CONF) = g_conf_base;
    REG32(RTC_CONF) = g_conf_base | (window << 14) | START |
                      (is_write ? (value & 0x3FFFu) : DIR_READ);

    out[0] = REG32(INT_STATEN);
    out[1] = REG32(INT_STATEN);
    out[2] = REG32(INT_STATEN);
    out[3] = REG32(INT_STATEN);

    while (!(REG32(INT_STATEN) & RTC_RDY)) {
        if (++n >= POLL_LIMIT)
            break;
    }

    out[4] = n;
    out[5] = REG32(RTC_DATA) & 0x3FFFu;
    out[6] = REG32(RTC_CONF);
}

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)RESULT;
    uint32_t original;

    out[0] = MAGIC;
    out[1] = STATUS_RUNNING;

    g_conf_base = ((REG32(RTC_CONF) >> 19) << 19) | RTC_EN;
    REG32(RTC_CONF) = g_conf_base;
    for (volatile uint32_t i = 0; i < 0x20000u; i++)
        __asm__ volatile ("" : : : "memory");

    out[2] = REG32(RTC_CONF);
    out[3] = REG32(INT_STATEN);
    out[4] = REG32(RTC_DATA);
    out[5] = 0;

    sideband_xfer(WIN_ALARM1, 0, 0, &out[6]);
    original = out[6 + 5];

    sideband_xfer(WIN_ALARM1, 0x1555u, 1, &out[13]);
    sideband_xfer(WIN_ALARM1, 0, 0, &out[20]);

    sideband_xfer(WIN_ALARM1, 0x2AAAu, 1, &out[27]);
    sideband_xfer(WIN_ALARM1, 0, 0, &out[34]);

    sideband_xfer(WIN_ALARM1, original, 1, &out[41]);

    sideband_xfer(WIN_USB0, 0, 0, &out[48]);
    sideband_xfer(WIN_USB1, 0, 0, &out[55]);

    REG32(RTC_CONF) = g_conf_base;
    out[1] = 0;
}
