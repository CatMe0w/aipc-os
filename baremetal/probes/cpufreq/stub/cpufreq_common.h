#pragma once

#include <stdint.h>

#ifndef REG32
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif

#define SYSCTRL_BASE      0x08000000u
#define CLKDIV1           (SYSCTRL_BASE + 0x04u)
#define ANALOG_CTRL2      (SYSCTRL_BASE + 0x64u)
#define TIMER2_CTRL       (SYSCTRL_BASE + 0x1Cu)
#define TIMER2_LIVE       (SYSCTRL_BASE + 0x104u)

/* CLKDIV1 fields. m is [5:0], asic_clk is [8:6], n is read back at [20:17]. */
#define CLKDIV1_PLL1_EN   (1u << 12)
#define CLKDIV1_ASIC_AP_EN (1u << 14)
#define CLKDIV1_CPU_PLLCLK (1u << 15)

/* SDRAM_CFG2 holds the timings. SDRAM_CFG3 holds the refresh count and the DQS
 * and clock delays. */
#define SDRAM_BASE        0x2002D000u
#define SDRAM_CMD         (SDRAM_BASE + 0x00u)
#define SDRAM_CFG2        (SDRAM_BASE + 0x04u)
#define SDRAM_CFG3        (SDRAM_BASE + 0x08u)

/* Request strobe in the command register. The controller clears it when the
 * command retires. */
#define SDRAM_CMD_REQ     (1u << 20)

/* The ASIC clock is the memory clock on this part, thus any change to it must
 * happen between these two. */
#define SDRAM_SR_ENTER_1  0x40170000u
#define SDRAM_SR_ENTER_2  0x00110000u
#define SDRAM_SR_EXIT_1   0x40170000u
#define SDRAM_SR_EXIT_2   0x60170000u

/* Count field full scale, EN, LOAD. 26 bits at 12 MHz wrap after 5.59 s. */
#define TIMER2_RELOAD     0x0FFFFFFFu
#define TIMER_MASK        0x03FFFFFFu
#define TIMER_HZ          12000000u

#define OUT_BASE          0x32008000u

/* The strobe is self clearing hardware and nothing promises it clears. Never
 * spin on it without a bound. */
#define POLL_LIMIT        1000000u

static volatile uint32_t *const out = (volatile uint32_t *)OUT_BASE;

static inline uint32_t timer2_live(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

static inline void timer2_reload(void)
{
    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
}

/*
 * Selects PLL1 (248 MHz) or ASIC (124 MHz) as the CPU clock. Unlike a PLL or
 * ASIC divider change this leaves every peripheral clock alone, thus it needs
 * no L2 SRAM trampoline and DDR stays usable.
 *
 * Returns the poll count, or POLL_LIMIT if the strobe never cleared.
 */
static inline uint32_t set_cpu_src_pll1(uint32_t use_pll1)
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
