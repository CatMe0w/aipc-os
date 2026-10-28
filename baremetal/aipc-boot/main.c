#include "kbd.h"
#include "lcd.h"
#include "log.h"
#include "mmu.h"
#include "soc.h"
#include "boot.h"
#include "card.h"
#include "fade.h"
#include "sysinfo.h"
#include "timer.h"
#include "ui.h"

#define DDR_BASE      0x30000000u
#define DDR_SIZE      0x04000000u
#define SYSCTRL_BASE  0x08000000u
#define PERIPH_BASE   0x20000000u  /* LCD, MCI, SPI, NFC, L2, RAMCTRL */
#define L2_BUF_BASE   0x48000000u

/*
 * The framebuffer sections stay uncached, because the LCD controller does not
 * snoop the D-cache. See docs/soc/lcd.md.
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
    mmu_map(FB_BACK_ADDR, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(LOG_BASE, MMU_SECTION_SIZE, MMU_BUFFERED);

    mmu_map(SYSCTRL_BASE, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(PERIPH_BASE, MMU_SECTION_SIZE, MMU_DEVICE);
    /* SD and NAND reads land in the L2 buffer before they reach DDR. */
    mmu_map(L2_BUF_BASE, MMU_SECTION_SIZE, MMU_DEVICE);

    mmu_start();
}

void boot_hw_init(void)
{
    l2_init();
    /* 77 Hz, the rate that Linux uses. The fade-out looks smoother than at
     * 25.7 Hz. */
    lcd_init_divider(2);
    timer_init();
    map_memory();
    kbd_init();
}

void boot_main(void)
{
    power_hold();
    work_led(1);
    speaker_amp(0);

    uart_init();
    log_init();
    log_puts("aipc-boot\n");

    boot_hw_init();
    sysinfo_read();
    card_init();
    ui_run();
}
