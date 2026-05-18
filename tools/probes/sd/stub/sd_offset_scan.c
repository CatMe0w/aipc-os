/*
 * sd_offset_scan - scan all 32-bit aligned offsets 0x00..0x40 at MCI_BASE
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define BASE          0x20020000u
#define MCI(off)      REG32(BASE + (off))

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 128u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    uint32_t off, orig, z, f, r;

    clear_result();

    OUT[0] = 0x4D4F4646u; /* "MOFF" = MMC OFFset scan */
    OUT[1] = 1u;
    OUT[2] = BASE;

    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();

    for (off = 0; off <= 0x40u; off += 4u) {
        volatile uint32_t *p = OUT + 4u + (off / 4u) * 6u;

        p[0] = off;

        orig = MCI(off);
        p[1] = orig;

        MCI(off) = 0;
        z = MCI(off);
        p[2] = z;

        MCI(off) = 0xFFFFFFFFu;
        f = MCI(off);
        p[3] = f;

        MCI(off) = orig;
        r = MCI(off);
        p[4] = r;

        p[5] = 0;
    }
}
