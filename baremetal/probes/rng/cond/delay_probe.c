/*
 * Reads 0x48001580 under an interleaved set of inter-read delays, and times
 * every chunk against timer 2. See the README.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE       0x08000000u
#define TIMER2_CTRL        (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE        (SYSCTRL_BASE + 0x104u)

/* count = full scale, EN, LOAD. Free running 26 bit down counter at 12 MHz. */
#define TIMER2_RELOAD      0x0FFFFFFFu
#define TIMER_MASK         0x03FFFFFFu
#define TIMER_HZ           12000000u

#define NC_ADDR            0x48001580u

#define HEADER_BASE        0x32008000u
#define TIMING_BASE        0x32008100u
#define OUTPUT_BASE        0x32010000u

#define NDELAY             5u
#ifndef ROUNDS
#define ROUNDS             16u
#endif
#define CHUNK              128u

#define MAGIC              0x524E4732u          /* RNG2 */
#define VERSION            4u

/* Each delay always sits at the same point of a round, so the value and the
 * position are confounded. REVERSE_DELAYS swaps the order, and an effect that
 * follows the value shows up in both builds. */
#ifdef REVERSE_DELAYS
static const uint32_t delays[NDELAY] = { 4096u, 512u, 64u, 8u, 0u };
#else
static const uint32_t delays[NDELAY] = { 0u, 8u, 64u, 512u, 4096u };
#endif

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

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *tim = (volatile uint32_t *)(uintptr_t)TIMING_BASE;
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    uint32_t live0, live1;
    uint32_t r, d, i;

    hdr[0] = MAGIC;
    hdr[1] = VERSION;
    hdr[2] = 0xFFFFFFFFu;
    hdr[3] = NDELAY;
    hdr[4] = CHUNK;
    hdr[5] = ROUNDS;
    hdr[6] = NC_ADDR;
    hdr[7] = TIMER_HZ;
    hdr[8] = TIMING_BASE;
    hdr[9] = OUTPUT_BASE;
    hdr[10] = NDELAY * ROUNDS * CHUNK * 4u;
    for (i = 0; i < NDELAY; i++)
        hdr[16 + i] = delays[i];

    /* Start timer 2 and record that it actually moves. */
    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    live0 = tlive();
    spin(10000u);
    live1 = tlive();
    hdr[11] = live0;
    hdr[12] = live1;
    hdr[13] = (live0 - live1) & TIMER_MASK;

    hdr[2] = 1u;
    for (r = 0; r < ROUNDS; r++) {
        for (d = 0; d < NDELAY; d++) {
            uint32_t slot = d * ROUNDS + r;
            volatile uint32_t *dst = out + (uintptr_t)slot * CHUNK;
            uint32_t k = delays[d];
            uint32_t t0 = tlive();

            for (i = 0; i < CHUNK; i++) {
                spin(k);
                dst[i] = REG32(NC_ADDR);
            }

            tim[slot * 2u + 0u] = t0;
            tim[slot * 2u + 1u] = (t0 - tlive()) & TIMER_MASK;
        }
    }

    hdr[2] = 0u;
}
