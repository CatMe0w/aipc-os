#include <stdint.h>

#define ARM926_L1_ENTRIES          4096u
#define ARM926_SECTION_SHIFT       20u
#define ARM926_SECTION_SIZE        (1u << ARM926_SECTION_SHIFT)
#define ARM926_SECTION_MASK        0xFFF00000u

#define ARM926_SECTION_TYPE        0x00000002u
#define ARM926_SECTION_BIT4        0x00000010u
#define ARM926_SECTION_AP_WRITE    0x00000400u
#define ARM926_SECTION_AP_READ     0x00000800u
#define ARM926_SECTION_BUFFERABLE  0x00000004u
#define ARM926_SECTION_CACHEABLE   0x00000008u

#define ARM926_SECTION_RW          (ARM926_SECTION_TYPE | \
                                    ARM926_SECTION_BIT4 | \
                                    ARM926_SECTION_AP_WRITE | \
                                    ARM926_SECTION_AP_READ)
#define ARM926_SECTION_RW_CACHED   (ARM926_SECTION_RW | \
                                    ARM926_SECTION_BUFFERABLE | \
                                    ARM926_SECTION_CACHEABLE)

#define DDR_BASE                   0x30000000u
#define DDR_SIZE                   0x04000000u
#define SYSCTRL_BASE               0x08000000u
#define LCD_BASE                   0x20000000u

static uint32_t s_arm926_l1_table[ARM926_L1_ENTRIES]
    __attribute__((aligned(16384)));

extern void aipc_enable_mmu_cache(uint32_t ttbr0);

static void arm926_map_section(uint32_t virt, uint32_t phys, uint32_t flags)
{
    s_arm926_l1_table[virt >> ARM926_SECTION_SHIFT] =
        (phys & ARM926_SECTION_MASK) | flags;
}

void aipc_mmu_cache_init(void)
{
    uint32_t i;
    uint32_t phys;

    for (i = 0; i < ARM926_L1_ENTRIES; ++i)
        s_arm926_l1_table[i] = 0;

    for (phys = DDR_BASE; phys < DDR_BASE + DDR_SIZE; phys += ARM926_SECTION_SIZE)
        arm926_map_section(phys, phys, ARM926_SECTION_RW_CACHED);

    /* MMIO stays uncached. */
    arm926_map_section(SYSCTRL_BASE, SYSCTRL_BASE, ARM926_SECTION_RW);
    arm926_map_section(LCD_BASE, LCD_BASE, ARM926_SECTION_RW);

    aipc_enable_mmu_cache((uint32_t)s_arm926_l1_table);
}

uint32_t aipc_mmu_get_l1_entry(uint32_t va)
{
    return s_arm926_l1_table[va >> ARM926_SECTION_SHIFT];
}
