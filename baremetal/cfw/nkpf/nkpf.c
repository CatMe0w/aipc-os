#include "cfw.h"
#include "fbcon.h"
#include "layout.h"
#include "log.h"
#include "nkpf.h"

static void icache(int on)
{
    uint32_t cr;

    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (cr));
    cr = on ? cr | (1u << 12) : cr & ~(1u << 12);
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r" (cr));
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r" (0));
}

static void module_range(const char *module, pf_range_t *range)
{
    if (!rom_code_range(module, range))
        panic("No ROM module %s", module);
}

void nkpf_main(uint32_t entry)
{
    cfw_stage = CFW_NK;
    icache(1);
    log_resume();
    screen_init((volatile uint16_t *)FB_PHYS, FB_WIDTH, 0, 0, FB_WIDTH,
                FB_HEIGHT, 1, RGB565_BLACK, false);
    screen_set_color(RGB565_CYAN);
    printf("nkpf\n");
    screen_mark_banner();
    screen_set_color(RGB565_WHITE);
    printf("NK entry %p\n", entry);

    cfw_run(CFW_NK, module_range);

    screen_set_color(RGB565_GREEN);
    printf("Booting NK\n");
    icache(0);
}
