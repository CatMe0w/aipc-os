/*
 * (Almost vibe-coded) Boot menu UI.
 *
 * LVGL renders straight into the scanout framebuffer, so a flush only drains
 * the write buffer. This file tracks the selection and shows it with
 * LV_STATE_CHECKED, not with an LVGL input device: the keyboard gives discrete
 * key events, and nothing else in the menu needs an indev.
 */

#include "lvgl/lvgl.h"

#include "boot.h"
#include "kbd.h"
#include "lcd.h"
#include "mmu.h"
#include "sd.h"
#include "timer.h"
#include "ui.h"

#define ITEM_COUNT   3
#define ITEM_HEIGHT  84
#define ITEM_WIDTH   704
#define ITEM_GAP     16
#define ITEM_TOP     148

#define COLOR_BG        0x080B10
#define COLOR_BG_GRAD   0x121A26
#define COLOR_CARD      0x161E29
#define COLOR_CARD_SEL  0x1E2C3C
#define COLOR_ACCENT    0x38BDF8
#define COLOR_TEXT      0xE6EDF3
#define COLOR_TEXT_DIM  0x8A99A8
#define COLOR_ERROR     0xF87171

struct menu_item {
    const char *title;
    const char *detail;
    int (*action)(void);
};

static const struct menu_item items[ITEM_COUNT] = {
    { "Linux",      "Load zImage from SD", boot_linux   },
    { "GDB stub",   "Load gdbstub.bin from SD and wait for a debugger", boot_gdbstub },
    { "Windows CE", "Load the stock EBOOT from NAND", boot_wince   },
};

static lv_obj_t *cards[ITEM_COUNT];
static lv_obj_t *status_label;
static lv_obj_t *kbd_label;
static int selected;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    drain_write_buffer();
    lv_display_flush_ready(disp);
}

static void make_card(int index)
{
    lv_obj_t *card = lv_obj_create(lv_screen_active());

    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, ITEM_WIDTH, ITEM_HEIGHT);
    lv_obj_set_pos(card, 48, ITEM_TOP + index * (ITEM_HEIGHT + ITEM_GAP));
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_pad_left(card, 24, 0);
    lv_obj_set_style_pad_top(card, 14, 0);

    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD_SEL), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), LV_STATE_CHECKED);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, items[index].title);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *detail = lv_label_create(card);
    lv_label_set_text(detail, items[index].detail);
    lv_obj_set_style_text_color(detail, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 0, 32);

    lv_obj_t *marker = lv_label_create(card);
    lv_label_set_text(marker, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(marker, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(marker, LV_ALIGN_RIGHT_MID, -8, 0);

    cards[index] = card;
}

void ui_build(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(COLOR_BG_GRAD), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AIPC OS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_34, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_pos(title, 48, 44);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "aipc-boot (Party Version for AOSCC 2026)");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_set_pos(subtitle, 48, 92);

    lv_obj_t *rule = lv_obj_create(screen);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(rule, 96, 4);
    lv_obj_set_pos(rule, 48, 126);
    lv_obj_set_style_radius(rule, 2, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(COLOR_ACCENT), 0);

    for (int i = 0; i < ITEM_COUNT; ++i)
        make_card(i);

    // lv_obj_t *hint = lv_label_create(screen);
    // lv_label_set_text(hint, "Up / Down to select     Enter to boot");
    // lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_TEXT_DIM), 0);
    // lv_obj_set_pos(hint, 48, 436);

    kbd_label = lv_label_create(screen);
    lv_obj_set_style_text_color(kbd_label, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_label_set_text(kbd_label, "");
    lv_obj_align(kbd_label, LV_ALIGN_TOP_RIGHT, -48, 92);

    status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(status_label, "");
    // lv_obj_set_pos(status_label, 48, 406);
    lv_obj_set_pos(status_label, 48, 446);
}

static void select_item(int index)
{
    if (index < 0)
        index = ITEM_COUNT - 1;
    if (index >= ITEM_COUNT)
        index = 0;

    lv_obj_remove_state(cards[selected], LV_STATE_CHECKED);
    selected = index;
    lv_obj_add_state(cards[selected], LV_STATE_CHECKED);
}

static void set_status(const char *text, uint32_t color)
{
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    lv_label_set_text(status_label, text);
}

/* Paint before a blocking load: nothing refreshes the screen while the SD or
 * the NAND driver runs. */
static void set_status_now(const char *text, uint32_t color)
{
    set_status(text, color);
    lv_refr_now(NULL);
}

static void activate(void)
{
    int rc;

    set_status_now("Loading...", COLOR_ACCENT);
    rc = items[selected].action();

    lv_label_set_text_fmt(status_label, "%s failed, rc=%d",
                          items[selected].title, rc);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_ERROR), 0);

    /* boot_wince() gives the pads to the NAND controller, so take them back. */
    if (items[selected].action == boot_wince)
        (void)sd_init();
}

static void update_kbd_label(void)
{
    static int last = -1;
    int ready = kbd_ready();

    if (ready == last)
        return;
    last = ready;
    lv_label_set_text(kbd_label, ready ? "Up / Down to select, Enter to boot" : "waiting for keyboard");
}

void ui_run(int sd_rc)
{
    lv_init();
    lv_tick_set_cb(timer_ms);
    lv_delay_set_cb(timer_delay_ms);

    lv_display_t *disp = lv_display_create(FB_WIDTH, FB_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, (void *)(uintptr_t)FB_ADDR, NULL, FB_BYTES,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    ui_build();
    lv_obj_add_state(cards[0], LV_STATE_CHECKED);

    if (sd_rc)
        lv_label_set_text_fmt(status_label, "No SD card, rc=%d", sd_rc);

    for (;;) {
        uint8_t key;
        int pressed;

        kbd_poll();
        while (kbd_pop(&key, &pressed)) {
            if (!pressed)
                continue;
            switch (key) {
            case KBD_UP:
            case KBD_LEFT:
                select_item(selected - 1);
                break;
            case KBD_DOWN:
            case KBD_RIGHT:
                select_item(selected + 1);
                break;
            case KBD_HOME:
                select_item(0);
                break;
            case KBD_END:
                select_item(ITEM_COUNT - 1);
                break;
            case KBD_ENTER:
                activate();
                break;
            default:
                break;
            }
        }

        update_kbd_label();
        lv_timer_handler();
    }
}
