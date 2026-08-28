#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL_BASE    0x08000000u
#define SYSCTRL(off)    REG32(SYSCTRL_BASE + (off))

/* [10:0] level 2 enables, [26:16] level 2 raw status. Timer n uses enable bit
 * 6-n and status bit 22-n. */
#define L2_INT_REG      0x4cu
#define L2_TIMER_ENS    (0x1fu << 1)

#define CTRL_OFF(n)     (0x18u + 4u * (n))
#define LIVE_OFF(n)     (0x100u + 4u * (n))

#define COUNT_MASK      0x03ffffffu
#define CTRL_EN         (1u << 26)
#define CTRL_LOAD       (1u << 27)
#define CTRL_CLEAR      (1u << 28)
#define CTRL_STA        (1u << 29)

#define TIMER_HZ        12000000u

#define NTIMERS         5u
#define ZC_SAMPLES      1024u
#define CLR_SAMPLES     512u
#define RELOAD_SAMPLES  1024u
#define LAT_ITERS       4096u

#define REF_TIMER       0u      /* timer1, reference for the latency phase */
#define SUBJECT_TIMER   1u      /* timer2, subject of the single timer phases */

#define START_COUNT     0x00100000u
#define SWEEP_COUNT     (TIMER_HZ / 1000u)      /* 1 ms */
#define SWEEP_SPIN      500000u

#define RESULT_BASE     0x32008000u
#define RESULT_MAGIC    0x314b4d54u
#define RESULT_VERSION  2u

struct timer_result {
    uint32_t magic;
    uint32_t version;
    uint32_t complete;
    uint32_t timer_hz;

    uint32_t l2_entry;
    uint32_t ctrl_entry[NTIMERS];
    uint32_t live_entry[NTIMERS];

    /* Registers one word past the documented end of the block. Read only. */
    uint32_t past_end_ctrl;
    uint32_t past_end_live;

    /* Phase 1: write one bit at a time to the subject control register. */
    uint32_t bitscan_ctrl[32];
    uint32_t bitscan_live[32];

    /* Phase 2: which bits of a loaded count survive into the live register. */
    uint32_t width_pattern[4];
    uint32_t width_ctrl[4];
    uint32_t width_live[4];

    /* Phase 3: enabling with and without the LOAD bit. */
    uint32_t start_count;
    uint32_t noload_live[8];
    uint32_t noload_ctrl;
    uint32_t load_live[8];
    uint32_t load_ctrl;

    /* Phase 4: cost of one capture loop pass, measured with the same loop. */
    uint32_t cal_first;
    uint32_t cal_last;
    uint32_t cal_samples;

    /* Phase 5: what the counter does when it reaches zero. */
    uint32_t zc_count;
    uint32_t zc_samples;
    uint32_t zc_live[ZC_SAMPLES];
    uint32_t zc_ctrl[ZC_SAMPLES];

    /* Phase 6: restart by writing CLEAR and EN only, the AK98 idiom. */
    uint32_t clr_ctrl_before;
    uint32_t clr_ctrl_after;
    uint32_t clr_samples;
    uint32_t clr_live[CLR_SAMPLES];
    uint32_t clr_ctrl[CLR_SAMPLES];

    /* Phase 7: the same short count on every timer. */
    uint32_t spin_ticks;
    uint32_t sweep_live_armed[NTIMERS];
    uint32_t sweep_live_later[NTIMERS];
    uint32_t sweep_delta[NTIMERS];
    uint32_t sweep_ctrl_expired[NTIMERS];
    uint32_t sweep_live_expired[NTIMERS];
    uint32_t sweep_l2_expired[NTIMERS];
    uint32_t sweep_l2_cleared[NTIMERS];

    /* Phase 8: read cost in timer ticks over LAT_ITERS passes. */
    uint32_t lat_iters;
    uint32_t lat_empty;
    uint32_t lat_live;
    uint32_t lat_ctrl;
    uint32_t lat_pair;
    uint32_t lat_dram;

    /* Phase 9: acknowledge with a single write, no read modify write. */
    uint32_t ack_count;
    uint32_t ack_sta_seen;
    uint32_t ack_ctrl_expired;
    uint32_t ack_ctrl_after;
    uint32_t ack_live[8];
    uint32_t off_sta_seen;
    uint32_t off_ctrl_after;
    uint32_t off_live[4];

    /*
     * Phase 10 and 11: where does the automatic reload take its value from,
     * the control register count field or a latch that only LOAD writes?
     * Phase 10 zeroes the count field while acknowledging, phase 11 writes the
     * period back. Each then watches one more reload.
     */
    uint32_t reload_count;
    uint32_t bare_ctrl_after;
    uint32_t bare_live[RELOAD_SAMPLES];
    uint32_t keep_ctrl_after;
    uint32_t keep_live[RELOAD_SAMPLES];
};

static struct timer_result *const res = (struct timer_result *)RESULT_BASE;

static void spin(uint32_t iters)
{
    for (volatile uint32_t i = 0; i < iters; i++)
        __asm__ volatile ("" : : : "memory");
}

static uint32_t live_of(uint32_t t)
{
    return SYSCTRL(LIVE_OFF(t)) & COUNT_MASK;
}

/* Clear, load a count, start. The three step sequence the WinCE OAL uses. */
static void arm(uint32_t t, uint32_t count)
{
    SYSCTRL(CTRL_OFF(t)) = CTRL_CLEAR;
    SYSCTRL(CTRL_OFF(t)) = count & COUNT_MASK;
    SYSCTRL(CTRL_OFF(t)) |= CTRL_EN | CTRL_LOAD;
}

static void stop(uint32_t t)
{
    SYSCTRL(CTRL_OFF(t)) = CTRL_CLEAR;
    SYSCTRL(CTRL_OFF(t)) = 0u;
}

/*
 * The capture loop. Phase 4 and phase 5 must use the same body, otherwise the
 * calibration does not predict how far the counter moves per pass.
 */
static void capture(uint32_t t, uint32_t *live, uint32_t *ctrl, uint32_t n)
{
    volatile uint32_t *lr = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + LIVE_OFF(t));
    volatile uint32_t *cr = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + CTRL_OFF(t));

    for (uint32_t i = 0; i < n; i++) {
        live[i] = *lr;
        ctrl[i] = *cr;
    }
}

static void phase_entry(void)
{
    res->magic = RESULT_MAGIC;
    res->version = RESULT_VERSION;
    res->complete = 0u;
    res->timer_hz = TIMER_HZ;

    res->l2_entry = SYSCTRL(L2_INT_REG);
    for (uint32_t t = 0; t < NTIMERS; t++) {
        res->ctrl_entry[t] = SYSCTRL(CTRL_OFF(t));
        res->live_entry[t] = SYSCTRL(LIVE_OFF(t));
    }
    res->past_end_ctrl = SYSCTRL(CTRL_OFF(NTIMERS));
    res->past_end_live = SYSCTRL(LIVE_OFF(NTIMERS));

    /* Keep every timer off the CPU for the rest of the run. Touch the five
     * enable bits only, never the status field. */
    SYSCTRL(L2_INT_REG) &= ~L2_TIMER_ENS;

    for (uint32_t t = 0; t < NTIMERS; t++)
        stop(t);
}

static void phase_bitscan(void)
{
    for (uint32_t n = 0; n < 32u; n++) {
        SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR;
        SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = 1u << n;
        res->bitscan_ctrl[n] = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
        res->bitscan_live[n] = SYSCTRL(LIVE_OFF(SUBJECT_TIMER));
    }
    stop(SUBJECT_TIMER);
}

static void phase_width(void)
{
    static const uint32_t patterns[4] = {
        0x03ffffffu, 0x02aaaaaau, 0x01555555u, 0x03000000u,
    };

    for (uint32_t i = 0; i < 4u; i++) {
        res->width_pattern[i] = patterns[i];
        SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR;
        SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = patterns[i];
        SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) |= CTRL_LOAD;
        res->width_ctrl[i] = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
        res->width_live[i] = SYSCTRL(LIVE_OFF(SUBJECT_TIMER));
    }
    stop(SUBJECT_TIMER);
}

static void phase_start(void)
{
    res->start_count = START_COUNT;

    /* Without LOAD. The DOOM timer helper and the touchpad probes do this. */
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR;
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = START_COUNT;
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = START_COUNT | CTRL_EN;
    for (uint32_t i = 0; i < 8u; i++)
        res->noload_live[i] = SYSCTRL(LIVE_OFF(SUBJECT_TIMER));
    res->noload_ctrl = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    stop(SUBJECT_TIMER);

    /* With LOAD. */
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR;
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = START_COUNT;
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = START_COUNT | CTRL_EN | CTRL_LOAD;
    for (uint32_t i = 0; i < 8u; i++)
        res->load_live[i] = SYSCTRL(LIVE_OFF(SUBJECT_TIMER));
    res->load_ctrl = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    stop(SUBJECT_TIMER);
}

/*
 * Run the capture over a count too large to reach zero, so the drop across the
 * whole window is the cost of the window itself.
 */
static uint32_t phase_calibrate(void)
{
    arm(SUBJECT_TIMER, COUNT_MASK);
    capture(SUBJECT_TIMER, res->zc_live, res->zc_ctrl, ZC_SAMPLES);

    res->cal_first = res->zc_live[0] & COUNT_MASK;
    res->cal_last = res->zc_live[ZC_SAMPLES - 1u] & COUNT_MASK;
    res->cal_samples = ZC_SAMPLES;
    stop(SUBJECT_TIMER);

    return (res->cal_first - res->cal_last) & COUNT_MASK;
}

static void phase_zero_crossing(uint32_t window)
{
    uint32_t count = window / 4u;

    if (count < 256u)
        count = 256u;

    res->zc_count = count;
    res->zc_samples = ZC_SAMPLES;

    arm(SUBJECT_TIMER, count);
    capture(SUBJECT_TIMER, res->zc_live, res->zc_ctrl, ZC_SAMPLES);
}

/*
 * Enter with the subject expired and its interrupt still pending, which is how
 * phase 5 leaves it. Writing CLEAR and EN is what the AK98 kernel calls a
 * reload. This shows whether that is what the hardware does.
 */
static void phase_clear_reload(void)
{
    res->clr_ctrl_before = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) |= CTRL_CLEAR | CTRL_EN;
    res->clr_ctrl_after = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));

    res->clr_samples = CLR_SAMPLES;
    capture(SUBJECT_TIMER, res->clr_live, res->clr_ctrl, CLR_SAMPLES);
    stop(SUBJECT_TIMER);
}

static void phase_sweep(void)
{
    uint32_t start;

    arm(REF_TIMER, COUNT_MASK);
    start = live_of(REF_TIMER);
    spin(SWEEP_SPIN);
    res->spin_ticks = (start - live_of(REF_TIMER)) & COUNT_MASK;
    stop(REF_TIMER);

    for (uint32_t t = 0; t < NTIMERS; t++) {
        arm(t, SWEEP_COUNT);
        res->sweep_live_armed[t] = SYSCTRL(LIVE_OFF(t));
        res->sweep_live_later[t] = SYSCTRL(LIVE_OFF(t));
        res->sweep_delta[t] = (res->sweep_live_armed[t] - res->sweep_live_later[t]) & COUNT_MASK;

        spin(SWEEP_SPIN);

        res->sweep_ctrl_expired[t] = SYSCTRL(CTRL_OFF(t));
        res->sweep_live_expired[t] = SYSCTRL(LIVE_OFF(t));
        res->sweep_l2_expired[t] = SYSCTRL(L2_INT_REG);

        stop(t);
        res->sweep_l2_cleared[t] = SYSCTRL(L2_INT_REG);
    }
}

static uint32_t measure_empty(void)
{
    uint32_t start = live_of(REF_TIMER);

    for (uint32_t i = 0; i < LAT_ITERS; i++)
        __asm__ volatile ("" : : : "memory");

    return (start - live_of(REF_TIMER)) & COUNT_MASK;
}

static uint32_t measure_one(volatile uint32_t *p)
{
    uint32_t start = live_of(REF_TIMER);

    for (uint32_t i = 0; i < LAT_ITERS; i++) {
        (void)*p;
        __asm__ volatile ("" : : : "memory");
    }

    return (start - live_of(REF_TIMER)) & COUNT_MASK;
}

static uint32_t measure_pair(volatile uint32_t *a, volatile uint32_t *b)
{
    uint32_t start = live_of(REF_TIMER);

    for (uint32_t i = 0; i < LAT_ITERS; i++) {
        (void)*a;
        (void)*b;
        __asm__ volatile ("" : : : "memory");
    }

    return (start - live_of(REF_TIMER)) & COUNT_MASK;
}

static void phase_latency(void)
{
    volatile uint32_t *live = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + LIVE_OFF(SUBJECT_TIMER));
    volatile uint32_t *ctrl = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + CTRL_OFF(SUBJECT_TIMER));
    volatile uint32_t *dram = (volatile uint32_t *)(uintptr_t)0x32004000u;

    arm(REF_TIMER, COUNT_MASK);
    arm(SUBJECT_TIMER, COUNT_MASK);

    res->lat_iters = LAT_ITERS;
    res->lat_empty = measure_empty();
    res->lat_live = measure_one(live);
    res->lat_ctrl = measure_one(ctrl);
    res->lat_pair = measure_pair(live, ctrl);
    res->lat_dram = measure_one(dram);

    stop(SUBJECT_TIMER);
    stop(REF_TIMER);
}

#define ACK_COUNT   4096u
#define ACK_POLLS   100000u

static uint32_t wait_sta(uint32_t t)
{
    for (uint32_t i = 0; i < ACK_POLLS; i++) {
        if (SYSCTRL(CTRL_OFF(t)) & CTRL_STA)
            return 1u;
    }
    return 0u;
}

/*
 * A periodic tick only has to acknowledge. Test whether one plain write does
 * it, with a zero count field, without disturbing the latched reload value.
 * Then test whether a plain CLEAR with EN low really stops the timer.
 */
static void phase_ack(void)
{
    res->ack_count = ACK_COUNT;

    arm(SUBJECT_TIMER, ACK_COUNT);
    res->ack_sta_seen = wait_sta(SUBJECT_TIMER);
    res->ack_ctrl_expired = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR | CTRL_EN;
    res->ack_ctrl_after = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    for (uint32_t i = 0; i < 8u; i++)
        res->ack_live[i] = SYSCTRL(LIVE_OFF(SUBJECT_TIMER));
    stop(SUBJECT_TIMER);

    arm(SUBJECT_TIMER, ACK_COUNT);
    res->off_sta_seen = wait_sta(SUBJECT_TIMER);
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR;
    res->off_ctrl_after = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    for (uint32_t i = 0; i < 4u; i++)
        res->off_live[i] = SYSCTRL(LIVE_OFF(SUBJECT_TIMER));
    stop(SUBJECT_TIMER);
}

static void capture_live(uint32_t t, uint32_t *live, uint32_t n)
{
    volatile uint32_t *lr = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + LIVE_OFF(t));

    for (uint32_t i = 0; i < n; i++)
        live[i] = *lr;
}

static void phase_reload_source(void)
{
    res->reload_count = ACK_COUNT;

    /* Acknowledge with a zero count field, then watch the next reload. */
    arm(SUBJECT_TIMER, ACK_COUNT);
    (void)wait_sta(SUBJECT_TIMER);
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = CTRL_CLEAR | CTRL_EN;
    res->bare_ctrl_after = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    capture_live(SUBJECT_TIMER, res->bare_live, RELOAD_SAMPLES);
    stop(SUBJECT_TIMER);

    /* Acknowledge with the period written back in the same word. */
    arm(SUBJECT_TIMER, ACK_COUNT);
    (void)wait_sta(SUBJECT_TIMER);
    SYSCTRL(CTRL_OFF(SUBJECT_TIMER)) = ACK_COUNT | CTRL_CLEAR | CTRL_EN;
    res->keep_ctrl_after = SYSCTRL(CTRL_OFF(SUBJECT_TIMER));
    capture_live(SUBJECT_TIMER, res->keep_live, RELOAD_SAMPLES);
    stop(SUBJECT_TIMER);
}

void stub_main(void)
{
    uint32_t window;

    phase_entry();
    phase_bitscan();
    phase_width();
    phase_start();
    window = phase_calibrate();
    phase_zero_crossing(window);
    phase_clear_reload();
    phase_sweep();
    phase_latency();
    phase_ack();
    phase_reload_source();

    for (uint32_t t = 0; t < NTIMERS; t++)
        stop(t);
    SYSCTRL(L2_INT_REG) = (SYSCTRL(L2_INT_REG) & ~L2_TIMER_ENS) |
                          (res->l2_entry & L2_TIMER_ENS);

    res->complete = 1u;
}
