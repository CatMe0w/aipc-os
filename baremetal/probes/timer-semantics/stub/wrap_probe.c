#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL_BASE    0x08000000u
#define SYSCTRL(off)    REG32(SYSCTRL_BASE + (off))

#define L2_INT_REG      0x4cu
#define L2_TIMER_ENS    (0x1fu << 1)

#define SUBJECT         1u      /* timer2 */
#define CTRL_OFF        (0x18u + 4u * SUBJECT)
#define LIVE_OFF        (0x100u + 4u * SUBJECT)

#define COUNT_MASK      0x03ffffffu
#define CTRL_EN         (1u << 26)
#define CTRL_LOAD       (1u << 27)
#define CTRL_CLEAR      (1u << 28)

#define WRAPS           4u
#define WINDOW          8u
#define MAX_ITERS       60000000u

#define RESULT_BASE     0x32008000u
#define RESULT_MAGIC    0x32524b54u
#define RESULT_VERSION  1u

struct wrap_result {
    uint32_t magic;
    uint32_t version;
    uint32_t complete;
    uint32_t loaded;

    uint32_t wraps_seen;
    uint32_t iters;
    uint32_t elapsed_lo;
    uint32_t elapsed_hi;

    uint32_t first_sample;
    uint32_t last_sample;
    uint32_t min_sample;
    uint32_t zero_seen;

    uint32_t wrap_iter[WRAPS];
    uint32_t wrap_prev[WRAPS];
    uint32_t wrap_cur[WRAPS];
    uint32_t wrap_ctrl[WRAPS];
    uint32_t wrap_cycle_lo[WRAPS];
    uint32_t wrap_cycle_hi[WRAPS];
    uint32_t wrap_before[WRAPS][WINDOW];
    uint32_t wrap_after[WRAPS][WINDOW];
};

static struct wrap_result *const res = (struct wrap_result *)RESULT_BASE;

static uint32_t g_last;
static uint32_t g_wrapped;
static uint32_t g_min;
static uint32_t g_zero;
static uint64_t g_elapsed;

static uint32_t sample(void)
{
    uint32_t cur = SYSCTRL(LIVE_OFF) & COUNT_MASK;

    g_elapsed += (uint64_t)((g_last - cur) & COUNT_MASK);
    g_wrapped = (cur > g_last) ? 1u : 0u;
    g_last = cur;

    if (cur < g_min)
        g_min = cur;
    if (cur == 0u)
        g_zero = 1u;

    return cur;
}

void stub_main(void)
{
    uint32_t ring[WINDOW];
    uint32_t ring_pos = 0u;
    uint64_t last_wrap_elapsed = 0u;
    uint32_t n = 0u;
    uint32_t l2_entry;
    uint32_t i;

    for (i = 0u; i < WINDOW; i++)
        ring[i] = 0u;

    res->magic = RESULT_MAGIC;
    res->version = RESULT_VERSION;
    res->complete = 0u;
    res->loaded = COUNT_MASK;

    /* Keep the timer off the CPU for the whole run. */
    l2_entry = SYSCTRL(L2_INT_REG);
    SYSCTRL(L2_INT_REG) &= ~L2_TIMER_ENS;

    SYSCTRL(CTRL_OFF) = CTRL_CLEAR;
    SYSCTRL(CTRL_OFF) = COUNT_MASK;
    SYSCTRL(CTRL_OFF) = COUNT_MASK | CTRL_EN | CTRL_LOAD;

    g_last = SYSCTRL(LIVE_OFF) & COUNT_MASK;
    g_min = g_last;
    g_zero = 0u;
    g_elapsed = 0u;
    res->first_sample = g_last;

    for (i = 0u; i < MAX_ITERS && n < WRAPS; i++) {
        uint32_t prev = g_last;
        uint32_t cur = sample();

        if (g_wrapped) {
            uint64_t cycle = g_elapsed - last_wrap_elapsed;

            last_wrap_elapsed = g_elapsed;
            res->wrap_iter[n] = i;
            res->wrap_prev[n] = prev;
            res->wrap_cur[n] = cur;
            res->wrap_ctrl[n] = SYSCTRL(CTRL_OFF);
            res->wrap_cycle_lo[n] = (uint32_t)cycle;
            res->wrap_cycle_hi[n] = (uint32_t)(cycle >> 32);

            for (uint32_t k = 0u; k < WINDOW; k++)
                res->wrap_before[n][k] = ring[(ring_pos + k) % WINDOW];
            for (uint32_t k = 0u; k < WINDOW; k++)
                res->wrap_after[n][k] = sample();

            n++;
        }

        ring[ring_pos] = cur;
        ring_pos = (ring_pos + 1u) % WINDOW;
    }

    res->wraps_seen = n;
    res->iters = i;
    res->elapsed_lo = (uint32_t)g_elapsed;
    res->elapsed_hi = (uint32_t)(g_elapsed >> 32);
    res->last_sample = g_last;
    res->min_sample = g_min;
    res->zero_seen = g_zero;

    SYSCTRL(CTRL_OFF) = CTRL_CLEAR;
    SYSCTRL(CTRL_OFF) = 0u;
    SYSCTRL(L2_INT_REG) = (SYSCTRL(L2_INT_REG) & ~L2_TIMER_ENS) |
                          (l2_entry & L2_TIMER_ENS);

    res->complete = 1u;
}
