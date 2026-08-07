/*
 * Sweeps RTC_CONF enable bits to find a working access sequence for the
 * RTC/USB indexed sideband, and captures the USB block's clock/function state.
 * Read strobes only.
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define SYSCTRL         0x08000000u
#define CLK_CON1        (SYSCTRL + 0x0Cu)
#define INT_STATEN      (SYSCTRL + 0x4Cu)
#define RTC_CONF        (SYSCTRL + 0x50u)
#define RTC_DATA        (SYSCTRL + 0x54u)
#define MULFUN_CON1     (SYSCTRL + 0x58u)

#define RTC_RDY         (1u << 24)      /* INT_STATEN: transfer done */
#define RTC_RDY_MASK    (1u << 8)       /* INT_STATEN: mask the ready IRQ */
#define RTC_EN          (1u << 24)      /* RTC_CONF: module enable */
#define RTC_WR_EN       (1u << 25)      /* RTC_CONF: register access enable */

#define STROBE_BOOTROM  0x60000u                    /* bits 18:17, AK7802 bootrom */
#define STROBE_AK98     ((1u << 21) | (2u << 18) | (1u << 17))

#define WIN_RTC0        (0u << 14)
#define WIN_USB0        (4u << 14)

#define USB             0x70000000u

#define RESULT          0x48000600u
#define MAGIC           0x55534231u
#define STATUS_RUNNING  0xDEADu
#define POLL_LIMIT      0x10000u

static uint32_t g_conf_entry;
static uint32_t g_staten_entry;

/* RTC_DATA doubles as the bootrom's boot stage marker; every read overwrites it. */
static uint32_t sideband_read(uint32_t window, uint32_t strobe, uint32_t enables,
                              volatile uint32_t *out)
{
    uint32_t n = 0;
    uint32_t base;

    REG32(RTC_CONF) = g_conf_entry | enables;
    base = (REG32(RTC_CONF) >> 19) << 19;
    REG32(RTC_CONF) = base;
    REG32(RTC_CONF) = REG32(RTC_CONF) | window | strobe;

    out[2] = REG32(RTC_CONF);

    while (!(REG32(INT_STATEN) & RTC_RDY)) {
        if (++n >= POLL_LIMIT)
            break;
    }

    out[0] = (n >= POLL_LIMIT) ? 0xFFFFFFFFu : (REG32(RTC_DATA) & 0x3FFFu);
    out[1] = n;
    out[3] = REG32(INT_STATEN);

    REG32(RTC_CONF) = g_conf_entry;
    return out[0];
}

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)RESULT;
    uint32_t i;

    static const uint32_t variant[5][2] = {
        { STROBE_BOOTROM, 0 },                    /* control: bootrom sequence */
        { STROBE_BOOTROM, RTC_WR_EN },
        { STROBE_BOOTROM, RTC_WR_EN | RTC_EN },
        { STROBE_BOOTROM, RTC_WR_EN },            /* plus the IRQ mask, below */
        { STROBE_AK98,    RTC_WR_EN },
    };

    out[0] = MAGIC;
    out[1] = STATUS_RUNNING;

    g_staten_entry = REG32(INT_STATEN);
    g_conf_entry = REG32(RTC_CONF);

    out[2] = REG32(CLK_CON1);
    out[3] = g_staten_entry;
    out[4] = g_conf_entry;
    out[5] = REG32(RTC_DATA);
    out[6] = REG32(MULFUN_CON1);

    out[7]  = REG8(USB + 0x00);
    out[8]  = REG8(USB + 0x01);
    out[9]  = REG32(USB + 0x330);
    out[10] = REG32(USB + 0x334);
    out[11] = REG32(USB + 0x338);
    out[12] = REG32(USB + 0x33C);
    out[13] = REG32(USB + 0x344);

    for (i = 0; i < 5; i++) {
        if (i == 3)
            REG32(INT_STATEN) = g_staten_entry | RTC_RDY_MASK;

        sideband_read(WIN_RTC0, variant[i][0], variant[i][1], &out[14 + i * 8]);
        sideband_read(WIN_USB0, variant[i][0], variant[i][1], &out[18 + i * 8]);

        if (i == 3)
            REG32(INT_STATEN) = g_staten_entry;
    }

    out[1] = 0;
}
