/*
 * Tests whether a /3 ASIC divider exists on the AK7802. See the README.
 *
 * Must stay a boot payload. The ASIC clock carries the L2 SRAM path that the
 * USB debug link runs on, thus the same code over a live GDB link loses the
 * link and the result with it.
 */

#include "asic3x_core.h"

/* Opt in: the control point is the only part that moves the ASIC clock, and a
 * clock change is what can stop the board. */
#ifndef ASIC3X_CONTROL
#define ASIC3X_CONTROL 0
#endif

#define L2FUNC __attribute__((noinline, section(".l2text")))
#define L2INLINE static inline __attribute__((always_inline))

/*
 * L2 SRAM is 0x48000000..0x4800157F. The stub starts after this runs, so stay
 * clear of what it owns: 0x48000000..0x480003FF (USB bulk windows),
 * 0x48001000..0x480010FF (UART), 0x48001500..0x4800153F (EP0).
 */
#define L2_CODE           0x48000400u
#define L2_CODE_LIMIT     0x48000C00u
#define L2_RESULT         0x48000C00u
#define L2_RESULT_WORDS   64u
#define L2_STACK_TOP      0x48000E60u

#define ALU_ITERS         500u
#define REG_ITERS         200u

#define ASIC_DIV_SHIFT    6u
#define ASIC_DIV_MASK     (7u << ASIC_DIV_SHIFT)
#define ASIC_DIV_QUARTER  (2u << ASIC_DIV_SHIFT)

/*
 * Two candidate register bits crossed with two candidate commit strobes, plus
 * one that sets everything. Four bits for each variant, packed so that the
 * table is an immediate. A real array would land in .rodata, outside the
 * section copied to L2 SRAM.
 */
#define V_SET64           0x1u
#define V_SET04           0x2u
#define V_STROBE12        0x4u
#define V_STROBE14        0x8u
#define V_COUNT           5u
#define V_PACKED          0x000F6A59u

#define M_POINTS          24u   /* r[24..55], four words for each point */

extern uint32_t __l2text_start;
extern uint32_t __l2text_end;

/* Defined in l2call.S. Runs fn on a stack in L2 SRAM. */
extern void l2_call(void (*fn)(volatile uint32_t *), volatile uint32_t *arg,
                    uint32_t stack_top);

L2INLINE uint32_t l2_timer_live(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

/* With the caches off, instruction fetch paces this loop, thus it measures the
 * ASIC clock and not the core clock. */
L2INLINE void l2_measure_alu(volatile uint32_t *dst)
{
    uint32_t k = ALU_ITERS;
    uint32_t start;

    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    start = l2_timer_live();

    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r"(k) :: "cc");

    dst[0] = ALU_ITERS;
    dst[1] = (start - l2_timer_live()) & TIMER_MASK;
}

/* Second instrument. The peripheral bus paces it, which is the ASIC clock
 * through a different path. */
L2INLINE void l2_measure_reg(volatile uint32_t *dst)
{
    uint32_t k = REG_ITERS;
    uint32_t start;

    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    start = l2_timer_live();

    do {
        (void)REG32(SYSCTRL_BASE + 0x00u);
        k--;
    } while (k != 0u);

    dst[0] = REG_ITERS;
    dst[1] = (start - l2_timer_live()) & TIMER_MASK;
}

L2INLINE uint32_t l2_strobe_asic(void)
{
    uint32_t spins = 0;

    REG32(CLKDIV1) = REG32(CLKDIV1) | CLKDIV1_ASIC_AP_EN;

    while ((REG32(CLKDIV1) & CLKDIV1_ASIC_AP_EN) != 0u && spins < POLL_LIMIT)
        spins++;

    return spins;
}

/* Commits with PLL_CHANGE_ENA instead, without touching the CPU source. */
L2INLINE uint32_t l2_strobe_pll1(void)
{
    uint32_t spins = 0;

    REG32(CLKDIV1) = REG32(CLKDIV1) | CLKDIV1_PLL1_EN;

    while ((REG32(CLKDIV1) & CLKDIV1_PLL1_EN) != 0u && spins < POLL_LIMIT)
        spins++;

    return spins;
}

L2INLINE void l2_spin(uint32_t n)
{
    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r"(n) :: "cc");
}

/*
 * The ASIC clock is the memory clock. Every clock change below must sit between
 * these two. Downward changes only: raising the clock needs the SDRAM_CFG2
 * timings recomputed first.
 */
L2INLINE void l2_ddr_enter_sr(void)
{
    REG32(SDRAM_CMD) = SDRAM_SR_ENTER_1;
    REG32(SDRAM_CMD) = SDRAM_SR_ENTER_2;
}

L2INLINE void l2_ddr_exit_sr(void)
{
    REG32(SDRAM_CMD) = SDRAM_SR_EXIT_1;
    REG32(SDRAM_CMD) = SDRAM_SR_EXIT_2;
}

/* Same sequence as set_cpu_src_pll1(), inlined so that nothing calls out of
 * L2 SRAM. */
L2INLINE uint32_t l2_set_cpu_src(uint32_t use_pll1)
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

/*
 * Runs from L2 SRAM. Must not touch DDR, must not call anything outside this
 * function, and must restore both registers on every path.
 */
L2FUNC void l2_worker(volatile uint32_t *r)
{
    uint32_t clkdiv_save = REG32(CLKDIV1);
    uint32_t analog_save = REG32(ANALOG_CTRL2);
    uint32_t cpu_on_asic;
    uint32_t i;

    for (i = 0; i < L2_RESULT_WORDS; i++)
        r[i] = 0;

    r[0] = clkdiv_save;
    r[1] = analog_save;

    /*
     * Every point runs with the CPU on the ASIC clock, which holds the CPU to
     * ASIC ratio at 1:1. The part supports 1:1 and 2:1 only, and a run that
     * reached 4:1 stopped the board.
     */
    cpu_on_asic = l2_set_cpu_src(0);
    r[2] = cpu_on_asic;
    r[3] = REG32(CLKDIV1);

    /* Point 0, reference. */
    l2_measure_alu(&r[M_POINTS + 0]);
    l2_measure_reg(&r[M_POINTS + 2]);

#if ASIC3X_CONTROL
    /* Point 1, control: /2 to /4 must read as a factor of two, or no later
     * point is evidence. It runs first because it is also the point most
     * likely to stop the board. */
    l2_ddr_enter_sr();
    REG32(CLKDIV1) = (REG32(CLKDIV1) & ~ASIC_DIV_MASK) | ASIC_DIV_QUARTER;
    r[4] = REG32(CLKDIV1);
    r[5] = l2_strobe_asic();
    l2_spin(0x200);
    l2_ddr_exit_sr();
    l2_measure_alu(&r[M_POINTS + 4]);
    l2_measure_reg(&r[M_POINTS + 6]);

    /* Point 2, control released. */
    l2_ddr_enter_sr();
    REG32(CLKDIV1) = (REG32(CLKDIV1) & ~ASIC_DIV_MASK) |
                     (clkdiv_save & ASIC_DIV_MASK);
    r[6] = l2_strobe_asic();
    l2_spin(0x200);
    l2_ddr_exit_sr();
    l2_measure_alu(&r[M_POINTS + 8]);
    l2_measure_reg(&r[M_POINTS + 10]);
#else
    r[4] = 0;
    r[5] = 0;
    r[6] = 0;
#endif

    /* Points 3 to 7, the arming variants. */
    for (i = 0; i < V_COUNT; i++) {
        uint32_t v = (V_PACKED >> (4u * i)) & 0xFu;
        uint32_t cd = REG32(CLKDIV1);

        l2_ddr_enter_sr();
        if (v & V_SET64)
            REG32(ANALOG_CTRL2) = analog_save | (1u << 28);
        if (v & V_SET04)
            REG32(CLKDIV1) = cd | (1u << 28);
        if (v & V_STROBE12)
            (void)l2_strobe_pll1();
        if (v & V_STROBE14)
            (void)l2_strobe_asic();
        l2_spin(0x200);
        l2_ddr_exit_sr();

        r[10 + i * 2] = REG32(CLKDIV1);
        r[11 + i * 2] = REG32(ANALOG_CTRL2);

        l2_measure_alu(&r[M_POINTS + 12 + i * 4]);
        l2_measure_reg(&r[M_POINTS + 14 + i * 4]);

        l2_ddr_enter_sr();
        REG32(ANALOG_CTRL2) = analog_save;
        REG32(CLKDIV1) = cd;
        (void)l2_strobe_pll1();
        (void)l2_strobe_asic();
        l2_spin(0x200);
        l2_ddr_exit_sr();
    }

    /* The divider is already back, so moving the CPU to PLL1 here goes 1:1 to
     * 2:1 and never through 4:1. */
    r[7] = REG32(CLKDIV1);
    r[8] = l2_set_cpu_src((clkdiv_save & CLKDIV1_CPU_PLLCLK) ? 1u : 0u);
    REG32(CLKDIV1) = clkdiv_save;
    REG32(ANALOG_CTRL2) = analog_save;
    r[9] = REG32(CLKDIV1);
    r[20] = REG32(ANALOG_CTRL2);
}

static void asic3x_run(volatile uint32_t *out)
{
    volatile uint32_t *src = (volatile uint32_t *)&__l2text_start;
    volatile uint32_t *end = (volatile uint32_t *)&__l2text_end;
    volatile uint32_t *dst = (volatile uint32_t *)L2_CODE;
    volatile uint32_t *res = (volatile uint32_t *)L2_RESULT;
    uint32_t offset = (uint32_t)(uintptr_t)&l2_worker -
                      (uint32_t)(uintptr_t)&__l2text_start;
    uint32_t size = (uint32_t)((const uint8_t *)end - (const uint8_t *)src);
    uint32_t i;

    out[0] = 0;
    out[1] = size;
    out[2] = offset;
    out[3] = 0;

    if (L2_CODE + size > L2_CODE_LIMIT) {
        out[0] = ASIC3X_COPY_TOO_BIG;
        return;
    }

    /* L2 SRAM ignores byte writes. Only word stores reach it. */
    for (i = 0; i < size / 4u; i++)
        dst[i] = src[i];

    for (i = 0; i < size / 4u; i++) {
        if (dst[i] != src[i]) {
            out[0] = ASIC3X_COPY_MISMATCH;
            out[3] = i;
            return;
        }
    }

    l2_call((void (*)(volatile uint32_t *))(uintptr_t)(L2_CODE + offset),
            res, L2_STACK_TOP);

    for (i = 0; i < L2_RESULT_WORDS; i++)
        out[4 + i] = res[i];

    out[0] = ASIC3X_MAGIC;
}

/* One shot at init, before USB and the LCD start. The stage entry points exist
 * only to satisfy probe_api. */
uint32_t probe_init(void)
{
    volatile uint32_t *out = (volatile uint32_t *)PROBE_RESULT_BASE;

    asic3x_run(out);
    return out[1];
}

void probe_stage(uint32_t stage)
{
    (void)stage;
}

void probe_trigger(void)
{
}
