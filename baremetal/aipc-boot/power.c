#include "boot.h"
#include "ff.h"
#include "lcd.h"
#include "mmio.h"
#include "power.h"
#include "soc.h"
#include "timer.h"
#include "ui_priv.h"

#define SYSCTRL(off)     REG32(0x08000000u + (off))
#define INT_MASK_IRQ     0x34u
#define INT_MASK_FIQ     0x38u
#define MULFUN_CON1      0x58u
#define  USB_SEL         0x00000007u

#define BOOTROM_NORMAL_BOOT  0x0000005Cu

/* The rail decays within this time when nothing else feeds it. The value
 * is not measured. */
#define RAIL_DECAY_MS    50u

#define COLOR_SAFE       0xFF8C00

/* Unmount first, so that every write reaches the card. */
static void stop_outputs(void)
{
    f_unmount("");
    speaker_amp(0);
    fade_out();
}

static void safe_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_t *l;

    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    l = ui_label(screen, &sans_22, COLOR_SAFE,
                 "It's now safe to unplug the USB cable.");
    lv_obj_center(l);
    lv_screen_load(screen);
    lv_refr_now(NULL);
}

void power_off(void)
{
    stop_outputs();
    power_release();
    while (power_key_down())
        ;
    timer_delay_ms(RAIL_DECAY_MS);
    safe_screen();
    for (;;)
        ;
}

void power_restart(void)
{
    stop_outputs();
    SYSCTRL(INT_MASK_IRQ) = 0;
    SYSCTRL(INT_MASK_FIQ) = 0;
    /* A live USB block writes into the L2 buffers that the bootrom uses. */
    SYSCTRL(MULFUN_CON1) &= ~USB_SEL;
    /* lcd_dark() waits for the backlight to go off before scan out stops.
     * Otherwise the panel flashes white. */
    lcd_dark();
    boot_jump(BOOTROM_NORMAL_BOOT);
}
