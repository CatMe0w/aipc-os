#include "lcd_probe_common.h"

void stub_main(void)
{
    lcd_init_from_eboot();
    build_l1_table(SECTION_RW);
    enable_mmu(0);
    fill_framebuffer(FB_CPU_BASE, 0x07E0u);
    drain_write_buffer();
    record_common(0x4C4D4D47u, 0x07E0u);
    disable_mmu_dcache();
}
