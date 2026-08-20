/*
 * Reads all six sideband windows, separating the two start bits to confirm
 * which one the hardware needs. Read strobes only.
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define SYSCTRL         0x08000000u
#define CLK_CON1        (SYSCTRL + 0x0Cu)
#define INT_STATEN      (SYSCTRL + 0x4Cu)
#define RTC_CONF        (SYSCTRL + 0x50u)
#define RTC_DATA        (SYSCTRL + 0x54u)
#define MULFUN_CON1     (SYSCTRL + 0x58u)

#define RTC_RDY         (1u << 24)
#define DIR_READ        (1u << 17)
#define START_A         (1u << 21)
#define START_B         (1u << 19)

#define WINDOWS         6u
#define USB             0x70000000u

#define RESULT          0x48000600u
#define MAGIC           0x55534232u
#define STATUS_RUNNING  0xDEADu
#define POLL_LIMIT      0x10000u
#define DELAY_ITERS     0x40000u

static uint32_t g_conf_entry;

/* out[0] = INT_STATEN right after strobe, to confirm done flag is per-transfer. */
static void sideband_read(uint32_t window, uint32_t strobe, volatile uint32_t *out)
{
    uint32_t n = 0;

    REG32(RTC_CONF) = (g_conf_entry >> 19) << 19;
    REG32(RTC_CONF) = REG32(RTC_CONF) | (window << 14) | strobe;

    out[0] = REG32(INT_STATEN);

    while (!(REG32(INT_STATEN) & RTC_RDY)) {
        if (++n >= POLL_LIMIT)
            break;
    }

    out[1] = n;
    out[2] = (n >= POLL_LIMIT) ? 0xFFFFFFFFu : (REG32(RTC_DATA) & 0x3FFFu);
}

static void delay(void)
{
    for (volatile uint32_t i = 0; i < DELAY_ITERS; i++)
        __asm__ volatile ("" : : : "memory");
}

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)RESULT;
    uint32_t i;

    out[0] = MAGIC;
    out[1] = STATUS_RUNNING;

    g_conf_entry = REG32(RTC_CONF);

    out[2] = REG32(CLK_CON1);
    out[3] = REG32(INT_STATEN);
    out[4] = g_conf_entry;
    out[5] = REG32(RTC_DATA);
    out[6] = REG32(MULFUN_CON1);
    out[7] = REG32(USB + 0x344);

    for (i = 0; i < WINDOWS; i++)
        sideband_read(i, START_A | START_B | DIR_READ, &out[8 + i * 3]);

    delay();

    for (i = 0; i < WINDOWS; i++)
        sideband_read(i, START_A | START_B | DIR_READ, &out[26 + i * 3]);

    /* Which of the two start bits the hardware needs, on the USB window. */
    sideband_read(4, START_A | DIR_READ, &out[44]);
    sideband_read(4, START_B | DIR_READ, &out[47]);
    sideband_read(4, DIR_READ, &out[50]);

    REG32(RTC_CONF) = g_conf_entry;
    out[1] = 0;
}
