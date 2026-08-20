/*
 * Timer2 in SYSCTRL, as used by the DOOM port. +0x1C holds the reload value
 * and the enable bit, +0x104 is the live down-counter. A read of +0x1C gives
 * the reload value back, not the counter, thus elapsed time is the sum of the
 * deltas of +0x104.
 *
 * If the counter never moves, timer_ms() counts its own calls instead. That
 * keeps the UI usable instead of frozen.
 */

#include "timer.h"

#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL(off)  REG32(0x08000000u + (off))

#define TIMER2_CTRL       0x1Cu
#define TIMER2_LIVE       0x104u
#define TIMER_COUNT_MASK  0x03FFFFFFu
#define TIMER_ENABLE      0x04000000u
#define TICKS_PER_MS      12000u

/* Rough stand-in tick when the hardware counter is dead: one millisecond per
 * this many timer_ms() calls. */
#define FALLBACK_CALLS_PER_MS  40u

static uint32_t s_last_raw;
static uint64_t s_total_ticks;
static uint32_t s_fallback_calls;
static int      s_hw_ok;

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
    s_fallback_calls = 0;

    for (uint32_t i = 0; i < 100000u; ++i) {
        if (read_live() != s_last_raw) {
            s_hw_ok = 1;
            break;
        }
    }
    s_last_raw = read_live();
}

void timer_stop(void)
{
    SYSCTRL(TIMER2_CTRL) = 0u;
}

uint32_t timer_ms(void)
{
    if (!s_hw_ok)
        return s_fallback_calls++ / FALLBACK_CALLS_PER_MS;

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
