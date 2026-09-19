/*
 * Samples four addresses in the unimplemented L2 window, for a series of short
 * runs taken while the board warms up from a cold start. See the README.
 *
 * The four cover active bits on both sides of a bias of 0.5, and the far pair
 * is the control: temperature is global and has to move all four.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE       0x08000000u
#define TIMER2_CTRL        (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE        (SYSCTRL_BASE + 0x104u)

#define TIMER2_RELOAD      0x0FFFFFFFu
#define TIMER_MASK         0x03FFFFFFu
#define TIMER_HZ           12000000u

#define HEADER_BASE        0x32008000u
#define TIMING_BASE        0x32008100u
#define OUTPUT_BASE        0x32010000u

#define NADDR              4u
#ifndef ROUNDS
#define ROUNDS             256u
#endif
#define CHUNK              128u
#define SPIN_CAL           10000u

#define MAGIC              0x524E4735u          /* RNG5 */
#define VERSION            7u

static const uint32_t addrs[NADDR] = {
    0x48001580u, 0x48001584u, 0x48001800u, 0x48001804u,
};

static inline uint32_t tlive(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

static inline void spin(uint32_t k)
{
    __asm__ volatile ("1: subs %0, %0, #1\n"
                      "   bne  1b\n"
                      : "+r"(k) :: "cc");
}

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *tim = (volatile uint32_t *)(uintptr_t)TIMING_BASE;
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    uint32_t r, a, i;

    hdr[0] = MAGIC;
    hdr[1] = VERSION;
    hdr[2] = 0xFFFFFFFFu;
    hdr[3] = NADDR;
    hdr[4] = CHUNK;
    hdr[5] = ROUNDS;
    hdr[6] = addrs[0];
    hdr[7] = TIMER_HZ;
    hdr[8] = TIMING_BASE;
    hdr[9] = OUTPUT_BASE;
    hdr[10] = NADDR * ROUNDS * CHUNK * 4u;
    for (i = 0; i < NADDR; i++)
        hdr[16 + i] = addrs[i];

    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    {
        uint32_t t0 = tlive();

        spin(SPIN_CAL);
        hdr[13] = (t0 - tlive()) & TIMER_MASK;
    }

    hdr[2] = 1u;
    for (r = 0; r < ROUNDS; r++) {
        for (a = 0; a < NADDR; a++) {
            uint32_t slot = a * ROUNDS + r;
            volatile uint32_t *dst = out + (uintptr_t)slot * CHUNK;
            volatile uint32_t *src =
                (volatile uint32_t *)(uintptr_t)addrs[a];
            uint32_t t0 = tlive();

            for (i = 0; i < CHUNK; i++)
                dst[i] = *src;

            tim[slot * 2u + 0u] = t0;
            tim[slot * 2u + 1u] = (t0 - tlive()) & TIMER_MASK;
        }
    }

    hdr[2] = 0u;
}
