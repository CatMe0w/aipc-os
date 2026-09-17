/*
 * Sweeps the DQS delay at a fixed memory clock. One call tests all sixteen
 * values. See the README.
 *
 * A DQS change needs no self refresh, because it only moves where read data is
 * sampled. A clock change does.
 */

#include "dqs_core.h"

#define L2FUNC __attribute__((noinline, section(".l2text")))
#define L2INLINE static inline __attribute__((always_inline))

#define L2_CODE           0x48000400u
#define L2_CODE_LIMIT     0x48000C00u
#define L2_TABLE          0x48000C00u
#define L2_RESULT         0x48000C40u
#define L2_STACK_TOP      0x48000E60u

#define DDR_TEST_BASE     0x32100000u
#define DDR_TEST_WORDS    1024u

#define CLKDIV1_M_MASK    0x3Fu
#define PLL1_M_ADD        62u

#define DQS_SHIFT         16u
#define DQS_MASK          (0xFu << DQS_SHIFT)
#define CLKD_SHIFT        20u
#define CLKD_MASK         (0xFu << CLKD_SHIFT)

extern uint32_t __l2text_start;
extern uint32_t __l2text_end;

extern void l2_call2(void (*fn)(volatile uint32_t *, volatile const uint32_t *),
                     volatile uint32_t *arg0, volatile const uint32_t *arg1,
                     uint32_t stack_top);

/*
 * PLL1 = 4 MHz * (m + 62) and memory = PLL1 / 2, thus the stages are 124, 150,
 * 160, 162, 164, 166, 168 and 170 MHz. Raise a stage_clkd entry to test the
 * clock delay, which otherwise stays at the boot value of 0.
 */
static const uint8_t stage_m[8]    = { 0, 13, 18, 19, 20, 21, 22, 23 };
static const uint8_t stage_clkd[8] = { 0,  0,  0,  0,  0,  0,  0,  0 };

/* Stage 8 applies PLL1 320 MHz, memory 160 MHz, DQS 2 and does not revert,
 * thus the debug session itself becomes the test. Stage 9 reverts. */
#define APPLY_M           18u
#define APPLY_DQS         2u
#define STAGE_APPLY       8u
#define STAGE_REVERT      9u

/* SDRAM_CFG2 timing fields, each as low bit, width, and the JEDEC minimum in
 * tenths of a ns: tRP, tRFC, tRCD, tWR, tRAS, tWTR. */
struct field { uint8_t lo, bits; uint16_t floor10; };
static const struct field timing_fields[] = {
    { 3,  3, 150 }, { 6,  4, 720 }, { 10, 3, 150 },
    { 13, 3, 150 }, { 19, 4, 400 }, { 23, 2, 100 },
};

L2INLINE uint32_t l2_timer_live(void)
{
    return REG32(TIMER2_LIVE) & TIMER_MASK;
}

L2INLINE void l2_spin(uint32_t n)
{
    __asm__ volatile ("1: subs %0, %0, #1\n   bne 1b\n" : "+r"(n) :: "cc");
}

L2INLINE uint32_t l2_measure(void)
{
    uint32_t k = 500u;
    uint32_t start;

    REG32(TIMER2_CTRL) = TIMER2_RELOAD;
    start = l2_timer_live();
    __asm__ volatile ("1: subs %0, %0, #1\n   bne 1b\n" : "+r"(k) :: "cc");
    return (start - l2_timer_live()) & TIMER_MASK;
}

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

L2INLINE uint32_t l2_set_pll_m(uint32_t m)
{
    uint32_t spins = 0;
    uint32_t v = (REG32(CLKDIV1) & ~CLKDIV1_M_MASK) | (m & CLKDIV1_M_MASK);

    REG32(CLKDIV1) = v;
    REG32(CLKDIV1) = v | CLKDIV1_PLL1_EN;
    l2_spin(16);
    while ((REG32(CLKDIV1) & CLKDIV1_PLL1_EN) != 0u && spins < POLL_LIMIT)
        spins++;
    return spins;
}

L2FUNC uint32_t l2_ddr_test(uint32_t seed)
{
    volatile uint32_t *p = (volatile uint32_t *)DDR_TEST_BASE;
    uint32_t i;
    uint32_t v = seed;
    uint32_t bad = 0;

    for (i = 0; i < DDR_TEST_WORDS; i++) {
        p[i] = v;
        v = v * 1664525u + 1013904223u;
    }
    v = seed;
    for (i = 0; i < DDR_TEST_WORDS; i++) {
        if (p[i] != v)
            bad++;
        v = v * 1664525u + 1013904223u;
    }
    return bad;
}

/* The result does not depend on the clock, thus a different result means the
 * core computed wrong. Multiplies as well as adds, to reach the multiplier. */
L2FUNC uint32_t l2_cpu_work(void)
{
    uint32_t a = 0x12345678u;
    uint32_t b = 0x9E3779B9u;
    uint32_t i;

    for (i = 0; i < 20000u; i++) {
        a = a * 1664525u + 1013904223u;
        b ^= a + (b << 7) + (b >> 3);
        a += b ^ (i * 2654435761u);
    }
    return a ^ b;
}

/* Applies or reverts the tuned point. Leaves the CPU on PLL1 either way. */
L2FUNC void l2_apply(volatile uint32_t *r, volatile const uint32_t *tbl)
{
    uint32_t m = tbl[1];
    uint32_t reg2 = tbl[2];
    uint32_t dqs = tbl[3];
    uint32_t cfg3 = REG32(SDRAM_CFG3);

    r[0] = REG32(CLKDIV1);
    r[1] = REG32(SDRAM_CFG2);
    r[2] = cfg3;
    r[3] = tbl[0];

    r[4] = l2_cpu_work();          /* before, at the entry clock */
    r[6] = l2_measure();

    /* DQS first, then the clock. The memory must never run fast with the
     * sampling point still set for the slow clock. */
    REG32(SDRAM_CFG3) = (cfg3 & ~(DQS_MASK | CLKD_MASK)) | (dqs << DQS_SHIFT);
    l2_spin(0x200);

    l2_ddr_enter_sr();
    r[7] = l2_set_pll_m(m);
    REG32(SDRAM_CFG2) = reg2;
    l2_spin(0x400);
    l2_ddr_exit_sr();

    r[5] = l2_cpu_work();          /* after, at the new clock */
    r[8] = l2_measure();
    r[9] = REG32(SDRAM_CFG2);
    r[10] = REG32(CLKDIV1);
    r[11] = REG32(SDRAM_CFG3);
    r[12] = l2_ddr_test(0xA5A50123u);
    r[13] = l2_ddr_test(0x5A5A0123u);
    r[14] = (r[4] == r[5]) ? 0u : 1u;
    r[15] = 1u;
}

L2FUNC void l2_stage(volatile uint32_t *r, volatile const uint32_t *tbl)
{
    uint32_t stage = tbl[0];
    uint32_t m = tbl[1];
    uint32_t reg2 = tbl[2];
    uint32_t clkd = tbl[3];
    uint32_t clkdiv_save = REG32(CLKDIV1);
    uint32_t reg2_save = REG32(SDRAM_CFG2);
    uint32_t cfg3_save = REG32(SDRAM_CFG3);
    uint32_t moved = (m != (clkdiv_save & CLKDIV1_M_MASK));
    uint32_t spins;
    uint32_t i;

    for (i = 0; i < DQS_RESULT_WORDS; i++)
        r[i] = 0;

    r[0] = clkdiv_save;
    r[1] = reg2_save;
    r[2] = cfg3_save;
    r[3] = stage;
    r[4] = m;
    r[5] = clkd;

    /* Put the CPU on the ASIC clock, so a failure belongs to the memory and
     * not to an overclocked core. */
    {
        uint32_t v = REG32(CLKDIV1) & ~(CLKDIV1_PLL1_EN | CLKDIV1_CPU_PLLCLK);

        spins = 0;
        REG32(CLKDIV1) = v;
        REG32(CLKDIV1) = v | CLKDIV1_PLL1_EN;
        while ((REG32(CLKDIV1) & CLKDIV1_PLL1_EN) != 0u && spins < POLL_LIMIT)
            spins++;
    }

    r[6] = l2_measure();

    if (moved) {
        l2_ddr_enter_sr();
        r[7] = l2_set_pll_m(m);
        REG32(SDRAM_CFG2) = reg2;
        l2_spin(0x400);
        l2_ddr_exit_sr();
    }

    r[8] = l2_measure();
    r[9] = REG32(SDRAM_CFG2);

    for (i = 0; i < DQS_VALUES; i++) {
        REG32(SDRAM_CFG3) = (cfg3_save & ~(DQS_MASK | CLKD_MASK)) |
                            (i << DQS_SHIFT) | (clkd << CLKD_SHIFT);
        l2_spin(0x200);
        r[DQS_META + i * 2] = l2_ddr_test(0xA5A50000u + i);
        r[DQS_META + i * 2 + 1] = l2_ddr_test(0x5A5A0000u + i);
    }

    REG32(SDRAM_CFG3) = cfg3_save;
    l2_spin(0x200);

    if (moved) {
        l2_ddr_enter_sr();
        (void)l2_set_pll_m(clkdiv_save & CLKDIV1_M_MASK);
        REG32(SDRAM_CFG2) = reg2_save;
        l2_spin(0x400);
        l2_ddr_exit_sr();
    }

    spins = 0;
    REG32(CLKDIV1) = clkdiv_save & ~CLKDIV1_PLL1_EN;
    REG32(CLKDIV1) = clkdiv_save | CLKDIV1_PLL1_EN;
    while ((REG32(CLKDIV1) & CLKDIV1_PLL1_EN) != 0u && spins < POLL_LIMIT)
        spins++;

    r[10] = REG32(CLKDIV1);
    r[11] = REG32(SDRAM_CFG2);
    r[12] = REG32(SDRAM_CFG3);
    r[13] = l2_measure();
    r[14] = l2_ddr_test(0xA5A5FFFFu);
    r[15] = 1u;
}

static uint32_t counts_for(uint32_t ns10, uint32_t asic_khz, uint32_t bits)
{
    uint32_t max = (1u << bits) - 1u;
    uint32_t n = (ns10 * asic_khz + 9999999u) / 10000000u;

    if (n < 1u)
        n = 1u;
    if (n > max)
        n = max;
    return n;
}

/* Recomputes every timing field for asic_khz. No field goes below the time it
 * already holds at base_khz, nor below its floor. */
static uint32_t reg2_for(uint32_t base, uint32_t asic_khz, uint32_t base_khz)
{
    uint32_t v = base;
    uint32_t i;

    for (i = 0; i < sizeof(timing_fields) / sizeof(timing_fields[0]); i++) {
        const struct field *f = &timing_fields[i];
        uint32_t max = (1u << f->bits) - 1u;
        uint32_t cur10 = ((base >> f->lo) & max) * 10000000u / base_khz;
        uint32_t target = (cur10 > f->floor10) ? cur10 : f->floor10;

        v = (v & ~(max << f->lo)) |
            (counts_for(target, asic_khz, f->bits) << f->lo);
    }
    return v;
}

static volatile uint32_t *const g_tbl = (volatile uint32_t *)L2_TABLE;
static uint32_t g_entry;
static uint32_t g_apply;
static uint32_t g_reg2_base;
static uint32_t g_boot_dqs;

uint32_t probe_init(void)
{
    volatile uint32_t *src = (volatile uint32_t *)&__l2text_start;
    volatile uint32_t *end = (volatile uint32_t *)&__l2text_end;
    volatile uint32_t *dst = (volatile uint32_t *)L2_CODE;
    uint32_t offset = (uint32_t)(uintptr_t)&l2_stage -
                      (uint32_t)(uintptr_t)&__l2text_start;
    uint32_t size = (uint32_t)((const uint8_t *)end - (const uint8_t *)src);
    uint32_t i;

    if (L2_CODE + size > L2_CODE_LIMIT)
        return DQS_COPY_TOO_BIG;

    /* L2 SRAM ignores byte writes. Only word stores reach it. */
    for (i = 0; i < size / 4u; i++)
        dst[i] = src[i];
    for (i = 0; i < size / 4u; i++) {
        if (dst[i] != src[i])
            return DQS_COPY_MISMATCH;
    }

    g_entry = L2_CODE + offset;
    g_apply = L2_CODE + (uint32_t)(uintptr_t)&l2_apply -
              (uint32_t)(uintptr_t)&__l2text_start;
    g_reg2_base = REG32(SDRAM_CFG2);
    g_boot_dqs = (REG32(SDRAM_CFG3) >> DQS_SHIFT) & 0xFu;
    return size;
}

void probe_stage(uint32_t stage)
{
    volatile uint32_t *out = (volatile uint32_t *)PROBE_RESULT_BASE;
    volatile uint32_t *res = (volatile uint32_t *)L2_RESULT;
    uint32_t m = (stage < 8u) ? stage_m[stage] : 0u;
    uint32_t asic_khz = (m + PLL1_M_ADD) * 4000u / 2u;
    uint32_t i;

    for (i = 0; i < DQS_RESULT_WORDS + 4u; i++)
        out[i] = 0;

    out[0] = DQS_MAGIC;
    out[1] = g_entry;
    out[2] = stage;

    if (!g_entry) {
        out[3] = DQS_COPY_TOO_BIG;
        return;
    }

    if (stage == STAGE_APPLY || stage == STAGE_REVERT) {
        uint32_t am = (stage == STAGE_APPLY) ? APPLY_M : 0u;
        uint32_t akhz = (am + PLL1_M_ADD) * 4000u / 2u;

        g_tbl[0] = stage;
        g_tbl[1] = am;
        g_tbl[2] = (am == 0u) ? g_reg2_base
                              : reg2_for(g_reg2_base, akhz, 124000u);
        g_tbl[3] = (stage == STAGE_APPLY) ? APPLY_DQS : g_boot_dqs;

        l2_call2((void (*)(volatile uint32_t *, volatile const uint32_t *))
                 (uintptr_t)g_apply, res, g_tbl, L2_STACK_TOP);
    } else {
        g_tbl[0] = stage;
        g_tbl[1] = m;
        g_tbl[2] = (m == 0u) ? g_reg2_base
                             : reg2_for(g_reg2_base, asic_khz, 124000u);
        g_tbl[3] = (stage < 8u) ? stage_clkd[stage] : 0u;

        l2_call2((void (*)(volatile uint32_t *, volatile const uint32_t *))
                 (uintptr_t)g_entry, res, g_tbl, L2_STACK_TOP);
    }

    for (i = 0; i < DQS_RESULT_WORDS; i++)
        out[4 + i] = res[i];

    out[3] = 0xC0FFEEu;
}

void probe_trigger(void)
{
    probe_stage(*(volatile uint32_t *)PROBE_STAGE_ADDR);
    __asm__ volatile (".word 0xE7FFDEFE");
}
