/*
 * ARM926EJ-S section table.
 *
 * Domain 0 is a manager domain, thus the hardware ignores the AP bits in each
 * section and no access can fault. Every mapping is identity, virtual equals
 * physical, because nothing in this project relocates anything.
 */

#include "mmu.h"

#define L1_ENTRIES     4096u
#define SECTION_SHIFT  20u
#define SECTION_MASK   0xFFF00000u

extern void mmu_enable(uint32_t ttbr0);

static uint32_t l1_table[L1_ENTRIES] __attribute__((aligned(16384)));

void mmu_reset(void)
{
    for (uint32_t i = 0; i < L1_ENTRIES; ++i)
        l1_table[i] = 0;
}

void mmu_map(uint32_t base, uint32_t size, uint32_t flags)
{
    uint32_t first = base >> SECTION_SHIFT;
    uint32_t count = (size + MMU_SECTION_SIZE - 1u) / MMU_SECTION_SIZE;

    if (count == 0)
        count = 1;

    for (uint32_t i = 0; i < count && first + i < L1_ENTRIES; ++i) {
        uint32_t addr = (first + i) << SECTION_SHIFT;

        l1_table[first + i] = (addr & SECTION_MASK) | flags;
    }
}

void mmu_start(void)
{
    mmu_enable((uint32_t)(uintptr_t)l1_table);
}
