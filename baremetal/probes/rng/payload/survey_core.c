/*
 * The whole cross-device comparison in one boot. See the README.
 *
 * The progress word advances after each phase, so a dump from a board that
 * stopped part way still yields the phases that finished.
 */

#include "probe_api.h"

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE   0x08000000u
#define TIMER2_CTRL    (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE    (SYSCTRL_BASE + 0x104u)
#define TIMER2_RELOAD  0x0FFFFFFFu
#define TIMER_MASK     0x03FFFFFFu
#define TIMER_HZ       12000000u

#define NC_BASE        0x48001580u
#define NC_END         0x48002000u
#define NC_WORDS       ((NC_END - NC_BASE) / 4u)      /* 672 */
#define DRIVE_ADDR     0x48000400u                    /* buffer 2 */

#define TIMING_BASE    0x32008100u
#define P1_BASE        0x32010000u
#define P2_BASE        0x32210000u
#define P3_BASE        0x322B8000u

#define NADDR          4u
#define P1_REPS        8u
#define P1_SAMPLES     16384u
#define P2_SAMPLES     256u
#define P3_SAMPLES     65536u

#define SPIN_CAL       10000u

#define MAGIC          0x53555256u                    /* SURV */
#define VERSION        1u

static const uint32_t addrs[NADDR] = {
    0x48001580u, 0x48001584u, 0x48001800u, 0x48001804u,
};

static inline uint32_t tlive(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

static void spin(uint32_t k)
{
    __asm__ volatile ("1: subs %0, %0, #1\n"
                      "   bne  1b\n"
                      : "+r"(k) :: "cc");
}

uint32_t probe_init(void)
{
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)PROBE_RESULT_BASE;
    volatile uint32_t *tim = (volatile uint32_t *)(uintptr_t)TIMING_BASE;
    volatile uint32_t *p1 = (volatile uint32_t *)(uintptr_t)P1_BASE;
    volatile uint32_t *p2 = (volatile uint32_t *)(uintptr_t)P2_BASE;
    volatile uint32_t *p3 = (volatile uint32_t *)(uintptr_t)P3_BASE;
    uint32_t saved_drive;
    uint32_t r, a, i, t0;

    out[0] = 0u;                 /* magic last, so a torn result stays invalid */
    out[1] = VERSION;
    out[2] = 0xFFFFFFFFu;        /* progress */
    out[3] = NADDR;
    out[4] = P1_REPS;
    out[5] = P1_SAMPLES;
    out[6] = P2_SAMPLES;
    out[7] = P3_SAMPLES;
    out[8] = NC_BASE;
    out[9] = NC_WORDS;
    out[10] = DRIVE_ADDR;
    out[11] = TIMER_HZ;
    out[12] = TIMING_BASE;
    out[13] = P1_BASE;
    out[14] = P2_BASE;
    out[15] = P3_BASE;
    for (i = 0; i < NADDR; i++)
        out[16 + i] = addrs[i];

    /* The stub leaves timer 2 stopped. The calibrated spin loop tells the host
     * whether this board's bus runs at the rate of the compared board, and
     * every timing result rests on that. */
    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    t0 = tlive();
    spin(SPIN_CAL);
    out[24] = (t0 - tlive()) & TIMER_MASK;
    out[25] = SPIN_CAL;

    /* Phase 1: four addresses, several passes, for active bits and drift. */
    out[2] = 1u;
    for (r = 0; r < P1_REPS; r++) {
        for (a = 0; a < NADDR; a++) {
            uint32_t slot = r * NADDR + a;
            volatile uint32_t *dst = p1 + (uintptr_t)slot * P1_SAMPLES;
            volatile uint32_t *src =
                (volatile uint32_t *)(uintptr_t)addrs[a];

            t0 = tlive();
            for (i = 0; i < P1_SAMPLES; i++)
                dst[i] = *src;
            tim[slot * 2u + 0u] = t0;
            tim[slot * 2u + 1u] = (t0 - tlive()) & TIMER_MASK;
        }
    }

    /* Phase 2: every word of the window, for the address bit 2 split. */
    out[2] = 2u;
    for (a = 0; a < NC_WORDS; a++) {
        volatile uint32_t *dst = p2 + (uintptr_t)a * P2_SAMPLES;
        volatile uint32_t *src =
            (volatile uint32_t *)(uintptr_t)(NC_BASE + a * 4u);

        for (i = 0; i < P2_SAMPLES; i++)
            dst[i] = *src;
    }

    /* Phase 3: drive a connected buffer between reads. A value that copied the
     * last word on the bus would show up here. */
    out[2] = 3u;
    saved_drive = REG32(DRIVE_ADDR);
    for (i = 0; i < P3_SAMPLES; i++) {
        REG32(DRIVE_ADDR) = (i & 1u) ? 0xFFFFFFFFu : 0u;
        __asm__ volatile ("" ::: "memory");
        p3[i] = REG32(NC_BASE);
    }
    REG32(DRIVE_ADDR) = saved_drive;

    out[2] = 0u;
    out[0] = MAGIC;
    return P1_REPS * NADDR * P1_SAMPLES;
}

void probe_stage(uint32_t stage)
{
    (void)stage;
}

void probe_trigger(void)
{
    __asm__ volatile (".word 0xE7FFDEFE");
}
