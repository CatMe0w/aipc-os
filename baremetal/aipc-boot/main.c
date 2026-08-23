#include "kbd.h"
#include "lcd.h"
#include "log.h"
#include "mmu.h"
#include "sd.h"
#include "soc.h"
#include "timer.h"
#include "ui.h"

#define DDR_BASE      0x30000000u
#define DDR_SIZE      0x04000000u
#define SYSCTRL_BASE  0x08000000u
#define PERIPH_BASE   0x20000000u   /* LCD, MCI, SPI, NFC, L2 control, RAMCTRL */
#define L2_BUF_BASE   0x48000000u

/*
 * The framebuffer section stays uncached, because the LCD controller does not
 * snoop the D-cache. See docs/tmp/lcd-mmu-doom-ahb.md.
 *
 * The log pool section is buffered but not cached, so a hang leaves the log
 * readable without a cache clean. A zImage also crosses this section, thus it
 * is buffered and not device.
 */
static void map_memory(void)
{
    mmu_reset();

    mmu_map(DDR_BASE, DDR_SIZE, MMU_CACHED);
    mmu_map(FB_ADDR, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(LOG_BASE, MMU_SECTION_SIZE, MMU_BUFFERED);

    mmu_map(SYSCTRL_BASE, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(PERIPH_BASE, MMU_SECTION_SIZE, MMU_DEVICE);
    /* SD and NAND reads land in the L2 buffer before they reach DDR. */
    mmu_map(L2_BUF_BASE, MMU_SECTION_SIZE, MMU_DEVICE);

    mmu_start();
}

void boot_main(void)
{
    power_hold();

    uart_init();
    log_init();
    log_puts("aipc-boot\n");

    l2_init();
    lcd_init();
    timer_init();
    map_memory();
    kbd_init();

    int sd_rc = sd_init();
    if (sd_rc) {
        log_puts("sd: no card");
        log_rc(sd_rc);
    }

    ui_run(sd_rc);
}
