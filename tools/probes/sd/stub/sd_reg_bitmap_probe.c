/*
 * sd_reg_bitmap_probe - discover writable bits in MCI registers
 *
 * Writes 0, then ~0, to each register, reads back.
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20020000u
#define MCI(off)      REG32(MCI_BASE + (off))

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

static void probe_reg(uint32_t base, uint32_t slot, uint32_t off, uint32_t label)
{
    volatile uint32_t *p = OUT + slot * 8u;

    p[0] = label;
    p[1] = off;

    uint32_t orig = REG32(base + off);
    p[2] = orig;

    REG32(base + off) = 0;
    p[3] = REG32(base + off);

    REG32(base + off) = 0xFFFFFFFFu;
    p[4] = REG32(base + off);

    REG32(base + off) = orig;
    p[5] = REG32(base + off);

    p[6] = 0;
    p[7] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x5242474Du; /* "RBGM" = ReGister BitMap */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* clock gate + reset */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();

    /* Probe MCI registers */
    probe_reg(MCI_BASE, 0, 0x04, 0x4D433034u); /* MC04 = MCI_CLOCK */
    probe_reg(MCI_BASE, 1, 0x0C, 0x4D433043u); /* MC0C = MCI_COMMAND */
    probe_reg(MCI_BASE, 2, 0x34, 0x4D433334u); /* MC34 = MCI_STATUS */
    probe_reg(MCI_BASE, 3, 0x38, 0x4D433338u); /* MC38 = MCI_MASK */
    probe_reg(MCI_BASE, 4, 0x3C, 0x4D433343u); /* MC3C = MCI_DMA_CTRL */

    /* Also try SDIO base at 0x20021000 */
    probe_reg(0x20021000u, 5, 0x04, 0x53443034u); /* SD04 */
    probe_reg(0x20021000u, 6, 0x34, 0x53443334u); /* SD34 */
}
