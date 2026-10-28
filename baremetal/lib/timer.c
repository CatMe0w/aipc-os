#include "timer.h"
#include "mmio.h"

#define SYSCTRL(off)  REG32(0x08000000u + (off))

#define TIMER2_CTRL       0x1Cu
#define TIMER2_LIVE       0x104u
#define TIMER_COUNT_MASK  0x03FFFFFFu
#define TIMER_ENABLE      0x04000000u
#define TICKS_PER_MS      12000u

static uint32_t s_last_raw;
static uint64_t s_total_ticks;

static uint32_t read_live(void)
{
    return SYSCTRL(TIMER2_LIVE) & TIMER_COUNT_MASK;
}

void timer_init(void)
{
    SYSCTRL(TIMER2_CTRL) = TIMER_COUNT_MASK;
    SYSCTRL(TIMER2_CTRL) = TIMER_COUNT_MASK | TIMER_ENABLE;

    s_last_raw = read_live();
    s_total_ticks = 0;
}

void timer_stop(void)
{
    SYSCTRL(TIMER2_CTRL) = 0u;
}

uint32_t timer_ms(void)
{
    uint32_t raw = read_live();
    s_total_ticks += (s_last_raw - raw) & TIMER_COUNT_MASK;
    s_last_raw = raw;
    return (uint32_t)(s_total_ticks / TICKS_PER_MS);
}

void timer_delay_ms(uint32_t ms)
{
    uint32_t start = timer_ms();

    while (timer_ms() - start < ms)
        ;
}
