/*
 * Reads 0x48001580 while the CPU drives traffic into connected L2 buffers.
 * See the README.
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

/* Buffer 2. */
#define L2_FAR             0x48000400u
/* Buffers 17 and 18, the last connected words before the window starts. */
#define L2_NEAR            0x48001500u

#define HEADER_BASE        0x32008000u
#define TIMING_BASE        0x32008100u
#define OUTPUT_BASE        0x32010000u

#define COND_QUIET         0u
#define COND_WR_FAR        1u
#define COND_WR_NEAR       2u
#define COND_RD_NEAR       3u
#define COND_BURST         4u
#define NCOND              5u

#ifndef ROUNDS
#define ROUNDS             256u
#endif
#define CHUNK              128u

#define NBURST             6u

#define MAGIC              0x524E4733u          /* RNG3 */
#define VERSION            5u

static const uint32_t burst[NBURST] = {
    0x48000400u, 0x48000600u, 0x48000800u,
    0x48000A00u, 0x48000C00u, 0x48000E00u,
};

static inline uint32_t tlive(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *tim = (volatile uint32_t *)(uintptr_t)TIMING_BASE;
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    uint32_t saved_burst[NBURST];
    uint32_t saved_near;
    uint32_t live0, live1;
    uint32_t r, c, i;

    for (i = 0; i < NBURST; i++)
        saved_burst[i] = REG32(burst[i]);
    saved_near = REG32(L2_NEAR);

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
    live0 = tlive();
    for (i = 10000u; i; i--)
        __asm__ volatile ("" ::: "memory");
    live1 = tlive();
    hdr[11] = live0;
    hdr[12] = live1;
    hdr[13] = (live0 - live1) & TIMER_MASK;

    hdr[2] = 1u;
    for (r = 0; r < ROUNDS; r++) {
        for (c = 0; c < NCOND; c++) {
            uint32_t slot = c * ROUNDS + r;
            volatile uint32_t *dst = out + (uintptr_t)slot * CHUNK;
            uint32_t t0 = tlive();

            for (i = 0; i < CHUNK; i++) {
                uint32_t pattern = (i & 1u) ? 0xFFFFFFFFu : 0u;
                uint32_t j;

                switch (c) {
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
                __asm__ volatile ("" ::: "memory");
                dst[i] = REG32(NC_ADDR);
            }

            tim[slot * 2u + 0u] = t0;
            tim[slot * 2u + 1u] = (t0 - tlive()) & TIMER_MASK;
        }
    }

    for (i = 0; i < NBURST; i++)
        REG32(burst[i]) = saved_burst[i];
    REG32(L2_NEAR) = saved_near;

    hdr[2] = 0u;
}
