#include "lcd_probe_common.h"

void stub_main(void)
{
    lcd_init_from_eboot();
    build_l1_table(SECTION_RW_CACHED);
    enable_mmu(1);
    fill_framebuffer(FB_CPU_BASE, 0x001Fu);
    drain_write_buffer();
    record_common(0x4C4D4442u, 0x001Fu);
    disable_mmu_dcache();
}
