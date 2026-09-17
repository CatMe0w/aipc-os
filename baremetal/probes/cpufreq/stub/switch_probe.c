/*
 * Moves the CPU clock between PLL1 (248 MHz) and ASIC (124 MHz) and stops
 * without putting it back, thus the debug session itself becomes the test.
 * USB runs from the 60 MHz clock and DDR from the ASIC clock, so neither one
 * moves here.
 *
 * Build with -DUSE_PLL1=1 to go back up.
 */

#include "cpufreq_common.h"

#define MAGIC 0x43505532u   /* CPU2 */

#ifndef USE_PLL1
#define USE_PLL1 0
#endif

void stub_main(void)
{
    uint32_t before = REG32(CLKDIV1);
    uint32_t spins = set_cpu_src_pll1(USE_PLL1);

    out[0] = MAGIC;
    out[1] = before;
    out[2] = spins;
    out[3] = REG32(CLKDIV1);
    out[4] = USE_PLL1;
}
