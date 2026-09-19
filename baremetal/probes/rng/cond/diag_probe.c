/*
 * Millisecond diagnostic. Checks that the read path works and that timer 2
 * runs, and writes no system controller register. See the README.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE  0x08000000u
#define CLKDIV1       (SYSCTRL_BASE + 0x04u)
#define TIMER2_CTRL   (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE   (SYSCTRL_BASE + 0x104u)

#define NC_ADDR       0x48001580u
#define HEADER_BASE   0x32008000u
#define BURST_BASE    0x32009000u
#define BURST_WORDS   1024u

#define MAGIC         0x524E4744u          /* RNGD */
#define VERSION       1u

static void spin(uint32_t k)
{
    __asm__ volatile ("1: subs %0, %0, #1\n"
                      "   bne  1b\n"
                      : "+r"(k) :: "cc");
}

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *burst = (volatile uint32_t *)(uintptr_t)BURST_BASE;
    uint32_t t0, t1, t2, i;

    hdr[0] = MAGIC;
    hdr[1] = VERSION;
    hdr[2] = 0xFFFFFFFFu;

    t0 = REG32(TIMER2_LIVE);
    spin(10000u);
    t1 = REG32(TIMER2_LIVE);
    spin(10000u);
    t2 = REG32(TIMER2_LIVE);

    hdr[3] = t0;
    hdr[4] = t1;
    hdr[5] = t2;
    hdr[6] = REG32(CLKDIV1);
    hdr[7] = REG32(TIMER2_CTRL);
    hdr[8] = NC_ADDR;
    hdr[9] = BURST_WORDS;

    for (i = 0; i < BURST_WORDS; i++)
        burst[i] = REG32(NC_ADDR);

    hdr[2] = 0u;
}
