#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE      0x08000000u
#define TIMER2_CTRL       0x0800001Cu
#define TIMER2_START      0x1FFFFFFFu
#define OUTPUT_BASE       0x48000400u
#define SAMPLES           4u
#define WORDS_PER_SAMPLE  128u
#define DELAY_ITERS       200000u

static void delay_between_samples(void)
{
    for (volatile uint32_t i = 0; i < DELAY_ITERS; i++)
        __asm__ volatile ("" : : : "memory");
}

void stub_main(void)
{
    volatile uint32_t *out = (volatile uint32_t *)OUTPUT_BASE;

    REG32(TIMER2_CTRL) = TIMER2_START;

    for (uint32_t sample = 0; sample < SAMPLES; sample++) {
        for (uint32_t word = 0; word < WORDS_PER_SAMPLE; word++)
            *out++ = REG32(SYSCTRL_BASE + word * 4u);

        delay_between_samples();
    }
}
