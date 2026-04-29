#ifndef LCD_PROBE_COMMON_H
#define LCD_PROBE_COMMON_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE            0x08000000u
#define LCD_BASE                0x20010000u
#define OUT_BASE                0x48001100u

#define FB_CPU_BASE             0x33B00000u
#define FB_LCD_BASE             0x07B00000u
#define FB_WIDTH                800u
#define FB_HEIGHT               480u
#define FB_PIXELS               (FB_WIDTH * FB_HEIGHT)

#define L1_TABLE_BASE           0x30004000u
#define L1_ENTRIES              4096u
#define SECTION_SHIFT           20u
#define SECTION_SIZE            (1u << SECTION_SHIFT)
#define SECTION_MASK            0xFFF00000u

#define SECTION_TYPE            0x00000002u
#define SECTION_BIT4            0x00000010u
#define SECTION_AP_WRITE        0x00000400u
#define SECTION_AP_READ         0x00000800u
#define SECTION_BUFFERABLE      0x00000004u
#define SECTION_CACHEABLE       0x00000008u

#define SECTION_RW              (SECTION_TYPE | SECTION_BIT4 | SECTION_AP_WRITE | SECTION_AP_READ)
#define SECTION_RW_CACHED       (SECTION_RW | SECTION_BUFFERABLE | SECTION_CACHEABLE)

#define DDR_BASE                0x30000000u
#define DDR_SIZE                0x04000000u
#define SYSCTRL_SECTION         0x08000000u
#define LCD_SECTION             0x20000000u
#define L2_SECTION              0x48000000u

static volatile uint32_t *const out = (volatile uint32_t *)OUT_BASE;

static inline void busy_wait(volatile uint32_t count)
{
    while (count-- != 0)
        ;
}

static inline void gpio_set_output(uint32_t pin, uint32_t value)
{
    uint32_t bank = (pin >> 5) & 3u;
    uint32_t bit = pin & 0x1Fu;
    volatile uint32_t *dir = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + 0x7C + 8u * bank);
    volatile uint32_t *data = (volatile uint32_t *)(uintptr_t)(SYSCTRL_BASE + 0x80 + 8u * bank);

    *dir &= ~(1u << bit);
    if (value)
        *data |= (1u << bit);
    else
        *data &= ~(1u << bit);
}

static inline void lcd_init_from_eboot(void)
{
    uint32_t clk_gate;

    REG32(SYSCTRL_BASE + 0x74) = 0x00000008u;
    REG32(SYSCTRL_BASE + 0x78) = 0x564F0010u;

    gpio_set_output(104, 1);
    gpio_set_output(69, 0);
    gpio_set_output(4, 0);

    clk_gate = REG32(SYSCTRL_BASE + 0x0C) & ~0x00000008u;
    REG32(SYSCTRL_BASE + 0x0C) = clk_gate | 0x00080000u;
    REG32(SYSCTRL_BASE + 0x0C) = clk_gate;

    REG32(LCD_BASE + 0x3C) = 0x00000000u;
    REG32(LCD_BASE + 0xE8) = 0x00000111u;
    REG32(LCD_BASE + 0x00) = 0x00000040u;

    REG32(LCD_BASE + 0x10) = 0x00300006u;
    REG32(LCD_BASE + 0x40) = 0x00080003u;
    REG32(LCD_BASE + 0x44) = 0x00058320u;
    REG32(LCD_BASE + 0x48) = 0x00050420u;
    REG32(LCD_BASE + 0x4C) = 0x00000018u;
    REG32(LCD_BASE + 0x50) = 0x00000001u;
    REG32(LCD_BASE + 0x54) = 0x00F00000u;
    REG32(LCD_BASE + 0x58) = 0x000001F9u;

    REG32(LCD_BASE + 0x00) = 0x80A80050u;

    REG32(LCD_BASE + 0xB0) = 0x000C81E0u;
    REG32(LCD_BASE + 0x14) = FB_LCD_BASE;
    REG32(LCD_BASE + 0x18) = 0x032001E0u;
    REG32(LCD_BASE + 0xA8) = 0x00000000u;
    REG32(LCD_BASE + 0xAC) = 0x000C81E0u;

    REG32(LCD_BASE + 0x00) = 0x80A80058u;
    REG32(LCD_BASE + 0xC8) = 0x00000800u;
    REG32(LCD_BASE + 0xB8) = 0x00000004u;

    busy_wait(1500000u);
    REG32(SYSCTRL_BASE + 0x2C) = 0x20D00E10u;
}

static inline void fill_framebuffer(uint32_t va, uint16_t color)
{
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)va;
    uint32_t i;

    for (i = 0; i < FB_PIXELS; ++i)
        fb[i] = color;
}

static inline void drain_write_buffer(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");
}

static inline uint32_t read_control(void)
{
    uint32_t value;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(value));
    return value;
}

static inline void map_section(uint32_t virt, uint32_t phys, uint32_t flags)
{
    volatile uint32_t *l1 = (volatile uint32_t *)L1_TABLE_BASE;
    l1[virt >> SECTION_SHIFT] = (phys & SECTION_MASK) | flags;
}

static inline uint32_t l1_entry(uint32_t virt)
{
    volatile uint32_t *l1 = (volatile uint32_t *)L1_TABLE_BASE;
    return l1[virt >> SECTION_SHIFT];
}

static inline void build_l1_table(uint32_t ddr_flags)
{
    volatile uint32_t *l1 = (volatile uint32_t *)L1_TABLE_BASE;
    uint32_t i;
    uint32_t phys;

    for (i = 0; i < L1_ENTRIES; ++i)
        l1[i] = 0;

    for (phys = DDR_BASE; phys < DDR_BASE + DDR_SIZE; phys += SECTION_SIZE)
        map_section(phys, phys, ddr_flags);

    map_section(FB_CPU_BASE, FB_CPU_BASE, SECTION_RW);
    map_section(SYSCTRL_SECTION, SYSCTRL_SECTION, SECTION_RW);
    map_section(LCD_SECTION, LCD_SECTION, SECTION_RW);
    map_section(L2_SECTION, L2_SECTION, SECTION_RW);
}

static inline void enable_mmu(uint32_t enable_dcache)
{
    uint32_t zero = 0;
    uint32_t ctl;

    __asm__ volatile ("mcr p15, 0, %0, c7, c7, 0" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(L1_TABLE_BASE) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c3, c0, 0" :: "r"(3u) : "memory");

    ctl = read_control();
    ctl &= ~0x00000002u;
    ctl |= 0x00000001u;
    ctl |= 0x00001000u;
    ctl |= 0x00000038u;
    if (enable_dcache)
        ctl |= 0x00000004u;
    else
        ctl &= ~0x00000004u;

    __asm__ volatile (
        "mcr p15, 0, %0, c1, c0, 0\n"
        "mrc p15, 0, %0, c1, c0, 0\n"
        : "+r"(ctl) :: "memory");
}

static inline void disable_mmu_dcache(void)
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

static inline void record_common(uint32_t magic, uint32_t color)
{
    out[0] = magic;
    out[1] = color;
    out[2] = read_control();
    out[3] = REG32(LCD_BASE + 0x00);
    out[4] = REG32(LCD_BASE + 0x14);
    out[5] = REG32(LCD_BASE + 0x18);
    out[6] = REG32(LCD_BASE + 0xAC);
    out[7] = REG32(LCD_BASE + 0xB0);
    out[8] = l1_entry(FB_CPU_BASE);
    out[9] = l1_entry(DDR_BASE);
}

#endif
