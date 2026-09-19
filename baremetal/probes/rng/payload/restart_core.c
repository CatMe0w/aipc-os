/*
 * One row of an SP 800-90B restart test. Every boot leaves the first samples
 * after power-on in DDR, and the host collects one row per power cycle. See
 * the README.
 */

#include "probe_api.h"

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE   0x08000000u
#define TIMER2_CTRL    (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE    (SYSCTRL_BASE + 0x104u)
#define TIMER2_RELOAD  0x0FFFFFFFu
#define TIMER_MASK     0x03FFFFFFu
#define TIMER_HZ       12000000u

#define NC_ADDR        0x48001580u

/* SP 800-90B asks for 1000 samples per restart. */
#define NSAMPLES       1000u
#define SAMPLE_OFFSET  16u

#define MAGIC          0x52535431u          /* RST1 */
#define VERSION        1u

uint32_t probe_init(void)
{
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)PROBE_RESULT_BASE;
    volatile uint32_t *dst = out + SAMPLE_OFFSET;
    volatile uint32_t *src = (volatile uint32_t *)(uintptr_t)NC_ADDR;
    uint32_t t0, i;

    out[0] = 0u;                    /* magic last, so a torn row stays invalid */
    out[1] = VERSION;
    out[2] = NSAMPLES;
    out[3] = NC_ADDR;
    out[6] = TIMER_HZ;
    out[7] = SAMPLE_OFFSET;

    /* The gdb stub leaves timer 2 stopped, so start it before timing. */
    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    t0 = REG32(TIMER2_LIVE) & TIMER_MASK;

    for (i = 0; i < NSAMPLES; i++)
        dst[i] = *src;

    out[4] = (t0 - (REG32(TIMER2_LIVE) & TIMER_MASK)) & TIMER_MASK;
    out[5] = t0;
    out[0] = MAGIC;
    return NSAMPLES;
}

/* The payload framework wants these. This experiment has no stages: every boot
 * does the same thing, and a stage would make rows differ. */
void probe_stage(uint32_t stage)
{
    (void)stage;
}

void probe_trigger(void)
{
    __asm__ volatile (".word 0xE7FFDEFE");
}
