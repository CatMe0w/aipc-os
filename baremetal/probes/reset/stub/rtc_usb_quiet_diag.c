#include <stdint.h>

#define REG8(addr)  (*(volatile uint8_t *)(uintptr_t)(addr))
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define CLOCK_GATE   (SYSCTRL_BASE + 0x0Cu)
#define INT_STATEN   (SYSCTRL_BASE + 0x4Cu)
#define RTC_CONF     (SYSCTRL_BASE + 0x50u)
#define RTC_DATA     (SYSCTRL_BASE + 0x54u)

#define USB_BASE     0x70000000u
#define USB_INTRTXE  (USB_BASE + 0x06u)
#define USB_INTRRXE  (USB_BASE + 0x08u)
#define USB_INTRUSBE (USB_BASE + 0x0Bu)

#define USB_GATE     (1u << 15)
#define RTC_READY    (1u << 24)
#define RTC_READ     (3u << 17)

#define RESULT       0x32008000u
#define MAGIC        0x52545155u
#define POLL_LIMIT   10000000u

static void delay(uint32_t count)
{
    while (count--)
        __asm__ volatile ("nop");
}

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)RESULT;
    uint32_t conf = REG32(RTC_CONF);
    uint32_t gate = REG32(CLOCK_GATE);
    uint32_t polls = 0;
    uint8_t intrtxe = REG8(USB_INTRTXE);
    uint8_t intrrxe = REG8(USB_INTRRXE);
    uint8_t intrusbe = REG8(USB_INTRUSBE);

    out[0] = MAGIC;
    out[1] = 0xFFFFFFFFu;
    out[2] = gate;
    out[3] = REG32(INT_STATEN);
    out[4] = conf;
    out[5] = REG32(RTC_DATA);

    REG8(USB_INTRTXE) = 0;
    REG8(USB_INTRRXE) = 0;
    REG8(USB_INTRUSBE) = 0;
    REG32(CLOCK_GATE) = gate | USB_GATE;
    delay(20000u);

    out[6] = REG32(CLOCK_GATE);
    out[7] = REG32(INT_STATEN);
    REG32(RTC_CONF) = (conf & ~0x7FFFFu) | RTC_READ | (4u << 14);

    while (!(REG32(INT_STATEN) & RTC_READY) && polls < POLL_LIMIT)
        polls++;

    out[8] = polls;
    out[9] = REG32(INT_STATEN);
    out[10] = REG32(RTC_CONF);
    out[11] = REG32(RTC_DATA);
    out[12] = REG32(RTC_DATA) & 0x3FFFu;
    out[13] = (polls < POLL_LIMIT) ? 1u : 0u;
    REG32(RTC_CONF) = conf;
    out[14] = REG32(RTC_CONF);

    REG32(CLOCK_GATE) = gate & ~USB_GATE;
    REG8(USB_INTRTXE) = intrtxe;
    REG8(USB_INTRRXE) = intrrxe;
    REG8(USB_INTRUSBE) = intrusbe;
    delay(20000u);
    out[15] = REG32(CLOCK_GATE);
    out[1] = 0;
}
