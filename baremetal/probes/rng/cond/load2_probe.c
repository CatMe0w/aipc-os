/*
 * Reads 0x48001580 under six L2 traffic conditions, all held at one inter-read
 * interval. The probe times each condition on the board first, then pads the
 * faster ones. See the README.
 *
 * Every address written here is saved on entry and put back before return.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE       0x08000000u
#define TIMER2_CTRL        (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE        (SYSCTRL_BASE + 0x104u)

#define TIMER2_RELOAD      0x0FFFFFFFu
#define TIMER_MASK         0x03FFFFFFu
#define TIMER_HZ           12000000u

#define NC_ADDR            0x48001580u
#define L2_FAR             0x48000400u          /* buffer 2 */
#define L2_NEAR            0x48001500u          /* buffers 17 and 18 */
#define DDR_SCRATCH        0x32100000u

#define HEADER_BASE        0x32008000u
#define TIMING_BASE        0x32008100u
#define OUTPUT_BASE        0x32010000u

#define COND_BARE          0u
#define COND_QUIET         1u
#define COND_WR_FAR        2u
#define COND_WR_NEAR       3u
#define COND_RD_NEAR       4u
#define COND_BURST         5u
#define NCOND              6u

#ifndef ROUNDS
#define ROUNDS             256u
#endif
#define CHUNK              128u
#define CAL_REPS           8u

#define NBURST             6u
#define SPIN_CAL           10000u

#define MAGIC              0x524E4734u          /* RNG4 */
#define VERSION            6u

static const uint32_t burst[NBURST] = {
    0x48000400u, 0x48000600u, 0x48000800u,
    0x48000A00u, 0x48000C00u, 0x48000E00u,
};

static inline uint32_t tlive(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

static inline void spin(uint32_t k)
{
    if (!k)
        return;
    __asm__ volatile ("1: subs %0, %0, #1\n"
                      "   bne  1b\n"
                      : "+r"(k) :: "cc");
}

/* ARM926EJ-S has no divide instruction and this image does not link libgcc,
 * so a runtime divisor needs its own shift and subtract loop. */
static uint32_t udiv(uint32_t n, uint32_t d)
{
    uint32_t q = 0u;
    uint32_t bit = 1u;

    if (!d)
        return 0u;
    while (d <= n && !(d & 0x80000000u)) {
        d <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (n >= d) {
            n -= d;
            q |= bit;
        }
        d >>= 1;
        bit >>= 1;
    }
    return q;
}

/* One chunk of CHUNK reads under condition c, with pad spin iterations before
 * each read. Both stages call this, so the loop bodies they time and the loop
 * bodies they measure are the same code. */
static void run_chunk(uint32_t c, volatile uint32_t *dst, uint32_t pad)
{
    uint32_t i;

    for (i = 0; i < CHUNK; i++) {
        uint32_t pattern = (i & 1u) ? 0xFFFFFFFFu : 0u;
        uint32_t j;

        switch (c) {
        case COND_QUIET:
            REG32(DDR_SCRATCH) = pattern;
            break;
        case COND_WR_FAR:
            REG32(L2_FAR) = pattern;
            break;
        case COND_WR_NEAR:
            REG32(L2_NEAR) = pattern;
            break;
        case COND_RD_NEAR:
            (void)REG32(L2_NEAR);
            break;
        case COND_BURST:
            for (j = 0; j < NBURST; j++)
                REG32(burst[j]) = pattern;
            break;
        default:
            break;
        }
        spin(pad);
        __asm__ volatile ("" ::: "memory");
        dst[i] = REG32(NC_ADDR);
    }
}

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *tim = (volatile uint32_t *)(uintptr_t)TIMING_BASE;
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    volatile uint32_t *scratch = (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    uint32_t saved_burst[NBURST];
    uint32_t saved_near, saved_scratch;
    uint32_t cal[NCOND], pad[NCOND];
    uint32_t spin_ticks, target;
    uint32_t r, c, i;

    for (i = 0; i < NBURST; i++)
        saved_burst[i] = REG32(burst[i]);
    saved_near = REG32(L2_NEAR);
    saved_scratch = REG32(DDR_SCRATCH);

    hdr[0] = MAGIC;
    hdr[1] = VERSION;
    hdr[2] = 0xFFFFFFFFu;
    hdr[3] = NCOND;
    hdr[4] = CHUNK;
    hdr[5] = ROUNDS;
    hdr[6] = NC_ADDR;
    hdr[7] = TIMER_HZ;
    hdr[8] = TIMING_BASE;
    hdr[9] = OUTPUT_BASE;
    hdr[10] = NCOND * ROUNDS * CHUNK * 4u;
    for (i = 0; i < NCOND; i++)
        hdr[16 + i] = i;

    REG32(TIMER2_CTRL) = TIMER2_RELOAD;

    /* Cost of one spin iteration, in timer ticks scaled by SPIN_CAL. */
    {
        uint32_t t0 = tlive();

        spin(SPIN_CAL);
        spin_ticks = (t0 - tlive()) & TIMER_MASK;
    }
    hdr[13] = spin_ticks;

    /* Stage 1: time every condition with no padding. */
    hdr[2] = 1u;
    target = 0u;
    for (c = 0; c < NCOND; c++) {
        uint32_t t0 = tlive();

        for (i = 0; i < CAL_REPS; i++)
            run_chunk(c, scratch, 0u);
        cal[c] = ((t0 - tlive()) & TIMER_MASK) / CAL_REPS;
        if (cal[c] > target)
            target = cal[c];
        hdr[24 + c] = cal[c];
    }
    hdr[14] = target;

    /* Stage 2: pad every condition up to the slowest one. */
    for (c = 0; c < NCOND; c++) {
        uint32_t deficit = target - cal[c];

        pad[c] = udiv(deficit * SPIN_CAL, spin_ticks * CHUNK);
        hdr[32 + c] = pad[c];
    }

    hdr[2] = 2u;
    for (r = 0; r < ROUNDS; r++) {
        for (c = 0; c < NCOND; c++) {
            uint32_t slot = c * ROUNDS + r;
            uint32_t t0 = tlive();

            run_chunk(c, out + (uintptr_t)slot * CHUNK, pad[c]);
            tim[slot * 2u + 0u] = t0;
            tim[slot * 2u + 1u] = (t0 - tlive()) & TIMER_MASK;
        }
    }

    for (i = 0; i < NBURST; i++)
        REG32(burst[i]) = saved_burst[i];
    REG32(L2_NEAR) = saved_near;
    REG32(DDR_SCRATCH) = saved_scratch;

    hdr[2] = 0u;
}
