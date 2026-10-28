#include "battery.h"
#include "mmio.h"
#include "timer.h"

#define SYSCTRL(off)        REG32(0x08000000u + (off))
#define CLKDIV2             0x08u
#define  ADC1_DIVIDER       0x00000007u
#define  ADC1_CLOCK         0x00000008u
#define  ADC1_NRESET        0x00400000u
#define  ADC1_POWER_DOWN    0x20000000u
#define ANALOG1             0x5Cu
#define  PD_REFERENCE       0x00000001u
#define  AD5_SEL1           0x20000000u
#define ADC1_TIMING         0x60u
#define  BIT_CYCLES         0x0000FFFFu
#define ANALOG2             0x64u
#define  ADC1_ENABLE        0x00000100u
#define  AD5_SEL2           0x00000800u
#define ADC1_STATUS         0x70u
#define  AD5_VALUE          0x000003FFu

#define SAMPLES       5
#define ZERO_RETRIES  100

static int ready;

static void adc_init(void)
{
    SYSCTRL(CLKDIV2) &= ~ADC1_NRESET;
    SYSCTRL(CLKDIV2) = (SYSCTRL(CLKDIV2) &
                        ~(ADC1_NRESET | ADC1_POWER_DOWN | ADC1_CLOCK |
                          ADC1_DIVIDER)) |
                       ADC1_NRESET | ADC1_CLOCK | 2u;
    SYSCTRL(ADC1_TIMING) = (SYSCTRL(ADC1_TIMING) & ~BIT_CYCLES) | 800u;
    SYSCTRL(ANALOG1) &= ~PD_REFERENCE;
    SYSCTRL(ANALOG2) |= ADC1_ENABLE;
    ready = 1;
}

int battery_mv(void)
{
    uint32_t sum = 0, val;
    int samples = 0, zeros = 0;

    if (!ready)
        adc_init();

    /* Bit 29 clear and bit 11 set select AD5. */
    SYSCTRL(ANALOG1) &= ~AD5_SEL1;
    timer_delay_ms(12);
    SYSCTRL(ANALOG2) |= AD5_SEL2;

    while (samples < SAMPLES) {
        val = SYSCTRL(ADC1_STATUS) & AD5_VALUE;
        if (val) {
            sum += val;
            samples++;
            timer_delay_ms(1);
        } else if (++zeros > ZERO_RETRIES) {
            break;
        } else {
            timer_delay_ms(2);
        }
    }

    SYSCTRL(ANALOG1) |= AD5_SEL1;
    SYSCTRL(ANALOG2) &= ~AD5_SEL2;

    if (samples < SAMPLES)
        return -1;
    /* 20 mV per step of 333 * raw / 1024 * 2, as in WinCE */
    return (int)(333u * (sum / SAMPLES) / 1024u * 2u * 20u);
}
