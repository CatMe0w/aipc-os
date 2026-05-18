/*
 * sd_base_scan - scan 0x2001xxxx..0x2003xxxx for MCI-like registers
 *
 * An MCI block has: +0x04 = CLOCK (readable, non-zero, writable),
 *                    +0x34 = STATUS (readable, non-zero).
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void probe_base(uint32_t slot, uint32_t base)
{
    volatile uint32_t *p = OUT + 4u + slot * 8u;

    p[0] = base;
    p[1] = REG32(base + 0x04);   /* CLOCK */
    p[2] = REG32(base + 0x34);   /* STATUS */
    p[3] = REG32(base + 0x0C);   /* COMMAND */
    p[4] = REG32(base + 0x08);   /* ARGUMENT */
    p[5] = REG32(base + 0x14);   /* RESP0 */
    p[6] = REG32(base + 0x00);   /* offset 0 */
    p[7] = 0;
}

void stub_main(void)
{
    uint32_t slot = 0;

    for (uint32_t i = 0; i < 128u; i++) OUT[i] = 0;
    OUT[0] = 0x5343414Eu; /* "SCAN" */
    OUT[1] = 1u;

    /* Enable MMC clock gate */
    SYSCTRL(0x0C) &= ~(1u << 2);
    SYSCTRL(0x0C) &= ~(1u << 8);  /* also SDIO clock gate */
    delay();

    /* Scan bases from 0x20010000 to 0x20030000 */
    static const uint32_t bases[] = {
        0x20020000u, 0x20021000u, 0x20022000u, 0x20023000u,
        0x20010000u, 0x20024000u, /* SPI for comparison */
        0x20025000u, 0x20026000u, /* UART for comparison */
        0x20027000u, 0x20028000u, 0x20029000u,
        0x2002A000u, /* NAND sequencer */
        0x2002B000u, /* NAND DMA */
        0x2002C000u, /* L2CTRL */
    };

    for (uint32_t i = 0; i < sizeof(bases)/sizeof(bases[0]); i++)
        probe_base(slot++, bases[i]);
}
