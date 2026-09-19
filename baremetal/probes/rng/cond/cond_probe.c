/*
 * Superseded. It crossed the delays with the two CPU clock sources. It does
 * not return, and the cause is not known. Keep it out of new work.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE       0x08000000u
#define CLKDIV1            (SYSCTRL_BASE + 0x04u)
#define TIMER2_CTRL        (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE        (SYSCTRL_BASE + 0x104u)

#define CLKDIV1_PLL1_EN    (1u << 12)
#define CLKDIV1_CPU_PLLCLK (1u << 15)
#define POLL_LIMIT         1000000u

#define TIMER2_RELOAD      0x0FFFFFFFu
#define TIMER_MASK         0x03FFFFFFu
#define TIMER_HZ           12000000u

#define NC_ADDR            0x48001580u

#define HEADER_BASE        0x32008000u
#define TIMING_BASE        0x32008100u
#define OUTPUT_BASE        0x32010000u

#define NDELAY             5u
#define NFREQ              2u
#define NCOND              (NDELAY * NFREQ)
#define CHUNK              128u
#define ROUNDS             64u

#define MAGIC              0x524E4731u          /* RNG1 */
#define VERSION            3u

static const uint32_t delays[NDELAY] = { 0u, 8u, 64u, 512u, 4096u };

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

/* Selects PLL1 or the ASIC clock for the CPU. Leaves every peripheral clock
 * alone, so DDR stays usable and no L2 trampoline is needed. */
static uint32_t set_cpu_src_pll1(uint32_t use_pll1)
{
    uint32_t v = REG32(CLKDIV1) & ~CLKDIV1_PLL1_EN;
    uint32_t spins = 0;

    if (use_pll1)
        v |= CLKDIV1_CPU_PLLCLK;
    else
        v &= ~CLKDIV1_CPU_PLLCLK;

    REG32(CLKDIV1) = v;
    REG32(CLKDIV1) = v | CLKDIV1_PLL1_EN;

    while ((REG32(CLKDIV1) & CLKDIV1_PLL1_EN) != 0u && spins < POLL_LIMIT)
        spins++;

    return spins;
}

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *tim = (volatile uint32_t *)(uintptr_t)TIMING_BASE;
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    uint32_t entry = REG32(CLKDIV1);
    uint32_t max_spins = 0;
    uint32_t r, f, d, i;

    hdr[0] = MAGIC;
    hdr[1] = VERSION;
    hdr[2] = 0xFFFFFFFFu;
    hdr[3] = NCOND;
    hdr[4] = NDELAY;
    hdr[5] = NFREQ;
    hdr[6] = CHUNK;
    hdr[7] = ROUNDS;
    hdr[8] = NC_ADDR;
    hdr[9] = TIMER_HZ;
    hdr[10] = 0u;
    hdr[11] = entry;
    hdr[12] = TIMING_BASE;
    hdr[13] = OUTPUT_BASE;
    hdr[14] = NCOND * ROUNDS * CHUNK * 4u;
    hdr[15] = 0u;
    for (i = 0; i < NDELAY; i++)
        hdr[16 + i] = delays[i];

    REG32(TIMER2_CTRL) = TIMER2_RELOAD;

    hdr[2] = 1u;
    for (r = 0; r < ROUNDS; r++) {
        for (f = 0; f < NFREQ; f++) {
            uint32_t spins = set_cpu_src_pll1(f == 0u ? 1u : 0u);

            if (spins > max_spins)
                max_spins = spins;

            for (d = 0; d < NDELAY; d++) {
                uint32_t c = f * NDELAY + d;
                uint32_t slot = c * ROUNDS + r;
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
    }

    set_cpu_src_pll1((entry & CLKDIV1_CPU_PLLCLK) ? 1u : 0u);
    hdr[15] = max_spins;
    hdr[2] = 0u;
}
