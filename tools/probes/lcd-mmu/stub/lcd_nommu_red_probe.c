#include "lcd_probe_common.h"

void stub_main(void)
{
    lcd_init_from_eboot();
    fill_framebuffer(FB_CPU_BASE, 0xF800u);
    drain_write_buffer();
    record_common(0x4C4E4D52u, 0xF800u);
}
