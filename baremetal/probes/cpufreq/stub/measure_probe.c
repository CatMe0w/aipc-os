/*
 * Measures what CLKDIV1 bit 15 does to the CPU clock, and whether DDR survives
 * the switch.
 *
 * Needs the I-cache. With it off, instruction fetch paces the loop and it
 * measures the ASIC clock instead of the core clock.
 *
 * The time base is SYSCTRL timer 2, from the 12 MHz crystal, which does not
 * follow PLL1.
 */

#include "cpufreq_common.h"

#define MAGIC             0x43505531u   /* CPU1 */

#define L1_TABLE_BASE     0x30004000u
#define L1_ENTRIES        4096u
#define SECTION_SHIFT     20u
#define SECTION_SIZE      (1u << SECTION_SHIFT)
#define SECTION_MASK      0xFFF00000u

#define SECTION_RW        0x00000C12u
#define SECTION_RW_CACHED (SECTION_RW | 0x0000000Cu)

#define DDR_BASE          0x30000000u
#define DDR_SIZE          0x04000000u

/* The two vector pages are here so that a fault inside the probe reaches the
 * GDB stub handlers instead of aborting forever on an unmapped vector fetch. */
static const uint32_t device_sections[] = {
    0x00000000u,        /* low vectors */
    0x08000000u,        /* SYSCTRL */
    0x20000000u,        /* MMC, SPI, UART, NAND, LCD */
    0x48000000u,        /* L2 buffer SRAM */
    0x70000000u,        /* USB */
    0xFFF00000u,        /* high vectors */
};

/* Scratch, clear of the probe image, the result block and the probe stack. */
#define DDR_TEST_BASE     0x32100000u
#define DDR_TEST_WORDS    1024u

#define WINDOW_TICKS      1200000u      /* 100 ms */
#define INNER_COUNT       5000u

struct sample {
    uint32_t batches;
    uint32_t ticks;
};

static void drain_write_buffer(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");
}

static uint32_t read_control(void)
{
    uint32_t value;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(value));
    return value;
}

static void map_section(uint32_t virt, uint32_t phys, uint32_t flags)
{
    volatile uint32_t *l1 = (volatile uint32_t *)L1_TABLE_BASE;
    l1[virt >> SECTION_SHIFT] = (phys & SECTION_MASK) | flags;
}

static void build_l1_table(void)
{
    volatile uint32_t *l1 = (volatile uint32_t *)L1_TABLE_BASE;
    uint32_t i;
    uint32_t phys;

    for (i = 0; i < L1_ENTRIES; ++i)
        l1[i] = 0;

    for (phys = DDR_BASE; phys < DDR_BASE + DDR_SIZE; phys += SECTION_SIZE)
        map_section(phys, phys, SECTION_RW_CACHED);

    for (i = 0; i < sizeof(device_sections) / sizeof(device_sections[0]); ++i)
        map_section(device_sections[i], device_sections[i], SECTION_RW);
}

/* The D-cache stays off, thus the result block and the DDR check reach DDR
 * with no cache clean. */
static void enable_mmu_icache(void)
{
    uint32_t zero = 0;
    uint32_t ctl;

    __asm__ volatile ("mcr p15, 0, %0, c7, c7, 0" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(L1_TABLE_BASE) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c3, c0, 0" :: "r"(3u) : "memory");

    ctl = read_control();
    ctl &= ~0x00000006u;        /* no alignment fault, D-cache off */
    ctl |= 0x00000001u;         /* MMU */
    ctl |= 0x00000038u;         /* SBO on ARM926 */
    ctl |= 0x00001000u;         /* I-cache */

    __asm__ volatile (
        "mcr p15, 0, %0, c1, c0, 0\n"
        "mrc p15, 0, %0, c1, c0, 0\n"
        : "+r"(ctl) :: "memory");
}

static void disable_mmu_icache(void)
{
    uint32_t zero = 0;
    uint32_t ctl = read_control();

    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");
    ctl &= ~0x0000100Du;
    __asm__ volatile (
        "mcr p15, 0, %0, c1, c0, 0\n"
        "mrc p15, 0, %0, c1, c0, 0\n"
        : "+r"(ctl) :: "memory");
    __asm__ volatile ("mcr p15, 0, %0, c7, c7, 0" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");
}

/* The one timer read in each batch is a bus access and does not scale with the
 * core clock. INNER_COUNT holds that below a few tenths of a percent. */
static void spin_batches(struct sample *s)
{
    uint32_t start;
    uint32_t elapsed;
    uint32_t batches = 0;

    timer2_reload();
    start = timer2_live();

    do {
        uint32_t k = INNER_COUNT;

        __asm__ volatile (
            "1: subs %0, %0, #1\n"
            "   bne  1b\n"
            : "+r"(k) :: "cc");

        batches++;
        elapsed = (start - timer2_live()) & TIMER_MASK;
    } while (elapsed < WINDOW_TICKS);

    s->batches = batches;
    s->ticks = elapsed;
}

static uint32_t ddr_check(uint32_t seed)
{
    volatile uint32_t *p = (volatile uint32_t *)DDR_TEST_BASE;
    uint32_t i;
    uint32_t bad = 0;

    for (i = 0; i < DDR_TEST_WORDS; i++)
        p[i] = seed ^ (i * 0x9E3779B9u);

    drain_write_buffer();

    for (i = 0; i < DDR_TEST_WORDS; i++)
        if (p[i] != (seed ^ (i * 0x9E3779B9u)))
            bad++;

    return bad;
}

void stub_main(void)
{
    uint32_t clkdiv1_entry = REG32(CLKDIV1);
    uint32_t analog2_entry = REG32(ANALOG_CTRL2);
    struct sample high, low, restored;
    uint32_t spins_low, spins_high;
    uint32_t read_low, read_high;
    uint32_t bad_high, bad_low, bad_restored;
    uint32_t ctl;

    build_l1_table();
    enable_mmu_icache();
    ctl = read_control();

    spin_batches(&high);
    bad_high = ddr_check(0x5A5A0001u);

    spins_low = set_cpu_src_pll1(0);
    read_low = REG32(CLKDIV1);
    spin_batches(&low);
    bad_low = ddr_check(0x5A5A0002u);

    spins_high = set_cpu_src_pll1(1);
    read_high = REG32(CLKDIV1);
    spin_batches(&restored);
    bad_restored = ddr_check(0x5A5A0003u);

    disable_mmu_icache();

    out[0] = MAGIC;
    out[1] = clkdiv1_entry;
    out[2] = analog2_entry;
    out[3] = WINDOW_TICKS;
    out[4] = INNER_COUNT;
    out[5] = ctl;
    out[6] = high.batches;
    out[7] = high.ticks;
    out[8] = bad_high;
    out[9] = spins_low;
    out[10] = read_low;
    out[11] = low.batches;
    out[12] = low.ticks;
    out[13] = bad_low;
    out[14] = spins_high;
    out[15] = read_high;
    out[16] = restored.batches;
    out[17] = restored.ticks;
    out[18] = bad_restored;
    out[19] = REG32(CLKDIV1);

    drain_write_buffer();
}
