#include <string.h>
#include "lvgl/lvgl.h"

#include "battery.h"
#include "card.h"
#include "entries.h"
#include "kbd.h"
#include "lcd.h"
#include "mmu.h"
#include "payload.h"
#include "power.h"
#include "settings.h"
#include "soc.h"
#include "storage.h"
#include "sysinfo.h"
#include "timer.h"
#include "ui.h"
#include "ui_priv.h"

#define ITEMS_MAX   24

#define BATTERY_POLL_MS  5000u
/* The power key: a shorter press is a click, a longer one shows the
 * progress, and the full time turns the device off. */
#define KEY_CLICK_MS     500u
#define KEY_OFF_MS       3000u

/* Carbon White, with two changes for this panel. The focus fill is two
 * steps lighter than Gray 20, because the panel shows it darker. The scrim
 * is opaque, see ui_modal.c, so it is a light gray close to Gray 20. */
static const struct theme light = {
    .bg = 0xFFFFFF, .fill = 0xEFEFEF, .divider = 0xDEDEDE,
    .panel = 0xF7F7F7, .scrim = 0xD6D6D6,
    .text = 0x000000, .text_2 = 0x6B6B6B, .text_3 = 0x9C9C9C,
    .focus = 0x0F62FE, .warn = 0xDA1E28, .button_2 = 0x393939,
    .toggle_off = 0x8C8C8C, .toggle_on = 0x24A148,
};

/* Carbon g100 with a darker background, because this panel shows the grays
 * near black unevenly */
static const struct theme dark = {
    .bg = 0x101010, .fill = 0x424242, .divider = 0x292929,
    .panel = 0x292929, .scrim = 0x000000,
    .text = 0xF7F7F7, .text_2 = 0xADADAD, .text_3 = 0x8C8C8C,
    .focus = 0xFFFFFF, .warn = 0xFF8389, .button_2 = 0x707070,
    .toggle_off = 0x707070, .toggle_on = 0x24A148,
};

const struct theme *ui_theme = &light;

static const struct page *const pages[] = {
    &page_launch, &page_payloads, &page_cfw, &page_tools,
    &page_info, &page_settings, &page_power,
};
#define PAGE_COUNT  ((int)(sizeof(pages) / sizeof(pages[0])))

static lv_style_t style_plain, style_focus, style_row, style_fill, style_ring;
static lv_style_t style_nav, style_nav_current;

static lv_obj_t *nav_items[PAGE_COUNT];
static lv_obj_t *list;
static lv_obj_t *foot_hints;
static lv_obj_t *foot_status;
static lv_obj_t *foot_progress;
static lv_obj_t *top_status;

static int page;
static int page_item[PAGE_COUNT];
static int in_content;

static lv_obj_t *items[ITEMS_MAX];
static int32_t item_top[ITEMS_MAX];   /* includes a section heading */
static int item_count;
static int item;

/* Where the next item goes in the list */
static int32_t cursor_y;
static int32_t pending_top = -1;      /* a section before the next item */

enum autoboot_state {
    AUTOBOOT_OFF,
    AUTOBOOT_WAIT_KBD,
    AUTOBOOT_RUN,
};

static enum autoboot_state autoboot;
static uint32_t autoboot_start_ms;
static uint32_t autoboot_ms;
static int autoboot_shown_s;
static int32_t autoboot_shown_w;

/* LVGL draws into the buffer that is not shown. After the last area of a
 * refresh, the buffer is complete, and it goes on screen at the next frame.
 * LVGL then copies the changed areas to the other buffer. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    drain_write_buffer();
    if (lv_display_flush_is_last(disp))
        lcd_flip((uint32_t)(uintptr_t)px_map);
    lv_display_flush_ready(disp);
}

static void init_styles(void)
{
    static int done;

    /* A theme change builds the styles again. */
    if (done) {
        lv_style_reset(&style_plain);
        lv_style_reset(&style_row);
        lv_style_reset(&style_fill);
        lv_style_reset(&style_ring);
        lv_style_reset(&style_focus);
        lv_style_reset(&style_nav);
        lv_style_reset(&style_nav_current);
    }
    done = 1;
    lv_style_init(&style_plain);
    lv_style_set_bg_opa(&style_plain, LV_OPA_COVER);
    lv_style_set_bg_color(&style_plain, lv_color_hex(COLOR_BG));
    lv_style_set_text_color(&style_plain, lv_color_hex(COLOR_TEXT));
    lv_style_set_text_font(&style_plain, &sans_18);

    /* Carbon contained list: a full-width divider between two rows */
    lv_style_init(&style_row);
    lv_style_set_border_side(&style_row, LV_BORDER_SIDE_TOP);
    lv_style_set_border_color(&style_row, lv_color_hex(COLOR_DIVIDER));
    lv_style_set_pad_hor(&style_row, PAD);
    /* All items have the primary text color. The fill and the ring mark the
     * focused one. */
    lv_style_set_text_color(&style_row, lv_color_hex(COLOR_TEXT));

    lv_style_init(&style_fill);
    lv_style_set_bg_opa(&style_fill, LV_OPA_COVER);
    lv_style_set_bg_color(&style_fill, lv_color_hex(COLOR_FILL));
    lv_style_set_text_color(&style_fill, lv_color_hex(COLOR_TEXT));

    lv_style_init(&style_ring);
    lv_style_set_outline_width(&style_ring, 2);
    lv_style_set_outline_pad(&style_ring, -2);
    lv_style_set_outline_color(&style_ring, lv_color_hex(COLOR_FOCUS));

    /* The focus of a row: a fill and a 2 px inset ring. An outline does not
     * move the content, as a border does. The divider above the row goes
     * under the fill. */
    lv_style_init(&style_focus);
    lv_style_set_bg_opa(&style_focus, LV_OPA_COVER);
    lv_style_set_bg_color(&style_focus, lv_color_hex(COLOR_FILL));
    lv_style_set_text_color(&style_focus, lv_color_hex(COLOR_TEXT));
    lv_style_set_border_opa(&style_focus, LV_OPA_TRANSP);
    lv_style_set_outline_width(&style_focus, 2);
    lv_style_set_outline_pad(&style_focus, -2);
    lv_style_set_outline_color(&style_focus, lv_color_hex(COLOR_FOCUS));

    lv_style_init(&style_nav);
    lv_style_set_text_color(&style_nav, lv_color_hex(COLOR_TEXT_2));

    /* The bar is the left border. A border moves the content, so the
     * negative padding keeps the labels where they are. */
    lv_style_init(&style_nav_current);
    lv_style_set_text_color(&style_nav_current, lv_color_hex(COLOR_TEXT));
    lv_style_set_border_side(&style_nav_current, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&style_nav_current, 4);
    lv_style_set_border_color(&style_nav_current, lv_color_hex(COLOR_BLUE));
    lv_style_set_pad_left(&style_nav_current, -4);
}

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                     int32_t h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

/* Plex Sans centers its line box, which has room for the descenders, so the
 * letters sit about 1 px above the center of an icon of the same height. */
static void text_offset(lv_obj_t *l, const lv_font_t *font)
{
    if (font != &icons_18 && font != &icons_20)
        lv_obj_set_style_translate_y(l, 1, 0);
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                   const char *text)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    text_offset(l, font);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/* A label that takes its color from its parent, for example a row whose
 * color changes with the focus */
static lv_obj_t *row_label(lv_obj_t *r, const lv_font_t *font,
                           const char *text)
{
    lv_obj_t *l = lv_label_create(r);

    lv_obj_remove_style_all(l);
    text_offset(l, font);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text);
    return l;
}

int32_t ui_text_width(const lv_font_t *font, const char *text)
{
    lv_point_t p;

    lv_text_get_size(&p, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

/* Footer */

struct hint {
    const char *keys;
    const char *action;
};

static int32_t keycap(int32_t x, const char *keys, const char *action)
{
    int32_t w = ui_text_width(&sans_16, keys) + 12;
    lv_obj_t *cap = box(foot_hints, x, (FOOT_H - 22) / 2, w, 22,
                        COLOR_DIVIDER);
    lv_obj_t *l;

    l = ui_label(cap, &sans_16, COLOR_TEXT_2, keys);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    x += w + 6;

    l = ui_label(foot_hints, &sans_16, COLOR_TEXT_3, action);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
    return x + ui_text_width(&sans_16, action) + 16;
}

static void set_hints(const struct hint *h, int n)
{
    int32_t x = PAD;

    lv_obj_clean(foot_hints);
    for (int i = 0; i < n; i++)
        if (h[i].action)
            x = keycap(x, h[i].keys, h[i].action);
}

void ui_hints_changed(void)
{
    const struct page *p = pages[page];
    struct hint h[4];
    int n = 0;

    if (!kbd_ready()) {
        lv_obj_clean(foot_hints);
        lv_obj_align(ui_label(foot_hints, &sans_16, COLOR_TEXT_3,
                              "Waiting for the keyboard"),
                     LV_ALIGN_LEFT_MID, PAD, 0);
        return;
    }

    if (modal_is_open()) {
        const char *enter, *esc;
        int choose;

        modal_hints(&enter, &esc, &choose);
        if (choose)
            h[n++] = (struct hint){ ARROW_LEFT ARROW_RIGHT, "Choose" };
        h[n++] = (struct hint){ "Enter", enter };
        h[n++] = (struct hint){ "Esc", esc };
    } else if (!in_content) {
        h[n++] = (struct hint){ ARROW_UP ARROW_DOWN, "Page" };
        if (item_count)
            h[n++] = (struct hint){ ARROW_RIGHT, "Open" };
    } else {
        const char *enter = NULL, *side = NULL;

        if (p->hints)
            p->hints(item, &enter, &side);
        h[n++] = (struct hint){ ARROW_UP ARROW_DOWN, "Move" };
        h[n++] = (struct hint){ ARROW_LEFT ARROW_RIGHT, side };
        h[n++] = (struct hint){ "Enter", enter };
        h[n++] = (struct hint){ "Esc", "Back" };
    }
    set_hints(h, n);
}

static void set_status(const char *text)
{
    lv_label_set_text(foot_status, text ? text : "");
    lv_obj_align(foot_status, LV_ALIGN_RIGHT_MID, -PAD, 0);
}

/* Boot */

void ui_dark(void)
{
    fade_out();
    lcd_dark();
}

static void redraw(void)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    lcd_backlight(settings_backlight());
}

void ui_resume(void)
{
    if (card_init())
        ui_card_changed();
    redraw();
}

void ui_light(void)
{
    /* Scan out runs before the redraw, because each flip waits for a frame. */
    lcd_light(0);
    redraw();
}

/* Top bar */

static int32_t top_item(int32_t right, const char *icon, const char *text)
{
    lv_obj_t *l = ui_label(top_status, &sans_16, COLOR_TEXT_3, text);
    int32_t w = ui_text_width(&sans_16, text);

    lv_obj_align(l, LV_ALIGN_LEFT_MID, right - w, 0);
    right -= w;
    if (icon) {
        l = ui_label(top_status, &icons_18, COLOR_TEXT_3, icon);
        right -= 4 + 18;
        lv_obj_align(l, LV_ALIGN_LEFT_MID, right, 0);
    }
    return right - 16;
}

/* Status that the top bar and the Info page share */

static int battery = -1;           /* mV */
static uint32_t battery_ms;
static struct ui_card card;

void ui_size_text(char *buf, size_t n, uint32_t mb)
{
    if (mb >= 1024)
        lv_snprintf(buf, n, "%u.%u GB", (unsigned)(mb / 1024),
                    (unsigned)(mb % 1024 * 10 / 1024));
    else
        lv_snprintf(buf, n, "%u MB", (unsigned)mb);
}

int ui_battery_mv(void)
{
    return battery;
}

void ui_battery_text(char *buf, size_t n)
{
    if (battery < 0)
        lv_snprintf(buf, n, "Unknown");
    else
        lv_snprintf(buf, n, "%d.%02d V", battery / 1000, battery % 1000 / 10);
}

const struct ui_card *ui_card(void)
{
    return &card;
}

static void update_top_bar(void)
{
    char text[32];
    const char *fw = sysinfo_fw_version();
    int32_t x = SCREEN_W - PAD;

    lv_obj_clean(top_status);

    ui_battery_text(text, sizeof(text));
    x = top_item(x, ICON_BATTERY, text);

    switch (card_state()) {
    case CARD_READY:
        if (card.space_ok) {
            ui_size_text(text, sizeof(text), card.free_mb);
            strcat(text, " free");
        } else {
            strcpy(text, "SD card");
        }
        break;
    case CARD_SETTLING:
        strcpy(text, "Reading SD card");
        break;
    case CARD_ERROR:
        strcpy(text, "SD card error");
        break;
    default:
        strcpy(text, "No SD card");
        break;
    }
    x = top_item(x, ICON_SD, text);

    lv_snprintf(text, sizeof(text), "FW %s%s", fw ? "v" : "",
                fw ? fw : "unknown");
    top_item(x, NULL, text);
}

/* Navigation. LV_STATE_CHECKED marks the current page with the blue bar.
 * LV_STATE_FOCUSED marks the focus with the fill and the ring, as on the
 * items. */

#define NAV_ITEM_H  48

static void update_nav(void)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_set_state(nav_items[i], LV_STATE_CHECKED, i == page);
        lv_obj_set_state(nav_items[i], LV_STATE_FOCUSED,
                         i == page && !in_content);
    }
}

static void build_nav(lv_obj_t *screen)
{
    lv_obj_t *nav = box(screen, 0, MAIN_Y, NAV_W, MAIN_H, COLOR_BG);

    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    lv_obj_set_style_border_color(nav, lv_color_hex(COLOR_DIVIDER), 0);

    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *n = box(nav, 0, 4 + i * NAV_ITEM_H, NAV_W - 1, NAV_ITEM_H,
                          COLOR_BG);
        lv_obj_t *l;

        lv_obj_set_style_bg_opa(n, LV_OPA_TRANSP, 0);
        lv_obj_add_style(n, &style_nav, 0);
        lv_obj_add_style(n, &style_nav_current, LV_STATE_CHECKED);
        lv_obj_add_style(n, &style_fill, LV_STATE_FOCUSED);
        lv_obj_add_style(n, &style_ring, LV_STATE_FOCUSED);
        l = row_label(n, &icons_20, pages[i]->icon);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, PAD, 0);
        l = row_label(n, &sans_18, pages[i]->name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, PAD + 20 + 12, 0);
        nav_items[i] = n;
    }
}

/* Page content */

/* The first row of a list has no divider above it. */
static int list_first;

/*
 * The focused item shows its help under its title, and the rows below it
 * move down. Each child of the list keeps its position without the
 * expansion in its user data.
 */
static lv_obj_t *item_help_obj[ITEMS_MAX];   /* hidden while not focused */
static int32_t item_extra[ITEMS_MAX];        /* height of the help */
static int expanded = -1;

static void place(lv_obj_t *o, int32_t y)
{
    lv_obj_set_y(o, y);
    lv_obj_set_user_data(o, (void *)(intptr_t)y);
}

static int32_t base_y(lv_obj_t *o)
{
    return (int32_t)(intptr_t)lv_obj_get_user_data(o);
}

/* The shift of a child at base y while an item is expanded */
static int32_t shift(int32_t y)
{
    if (expanded < 0 || y <= base_y(items[expanded]))
        return 0;
    return item_extra[expanded];
}

static void relayout(void)
{
    uint32_t n = lv_obj_get_child_count(list);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(list, (int32_t)i);

        lv_obj_set_y(c, base_y(c) + shift(base_y(c)));
    }
}

static void expand(int i)
{
    if (expanded >= 0) {
        lv_obj_set_height(items[expanded], ROW_H);
        lv_obj_add_flag(item_help_obj[expanded], LV_OBJ_FLAG_HIDDEN);
    }
    expanded = i >= 0 && item_help_obj[i] ? i : -1;
    if (expanded >= 0) {
        lv_obj_set_height(items[i], ROW_H + item_extra[i]);
        lv_obj_remove_flag(item_help_obj[i], LV_OBJ_FLAG_HIDDEN);
    }
    relayout();
}

void ui_section(const char *title)
{
    lv_obj_t *h, *l;

    if (pending_top < 0)
        pending_top = cursor_y;
    /* 16 px between stacked lists */
    if (cursor_y)
        cursor_y += PAD;
    h = box(list, 0, cursor_y, LIST_W, 36, COLOR_BG);
    place(h, cursor_y);
    lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(h, 1, 0);
    lv_obj_set_style_border_color(h, lv_color_hex(COLOR_DIVIDER), 0);
    l = ui_label(h, &sans_16, COLOR_TEXT_3, title);
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, PAD, -6);
    cursor_y += 36;
    list_first = 1;
}

void ui_note(const char *text)
{
    lv_obj_t *l = ui_label(list, &sans_18, COLOR_TEXT_2, text);

    lv_obj_set_width(l, LIST_W - 2 * PAD);
    lv_obj_set_style_text_line_space(l, LINE_SPACE, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(l, PAD);
    place(l, cursor_y + 10);
    cursor_y += ROW_H;
}

static lv_obj_t *add_item(lv_obj_t *o, int32_t y)
{
    if (item_count == ITEMS_MAX) {
        lv_obj_delete(o);
        return NULL;
    }
    item_top[item_count] = pending_top >= 0 ? pending_top : y;
    item_help_obj[item_count] = NULL;
    item_extra[item_count] = 0;
    pending_top = -1;
    items[item_count++] = o;
    return o;
}

static lv_obj_t *row(int item)
{
    lv_obj_t *r = lv_obj_create(list);

    lv_obj_remove_style_all(r);
    lv_obj_add_style(r, &style_row, 0);
    lv_obj_add_style(r, &style_focus, LV_STATE_FOCUSED);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_x(r, 0);
    place(r, cursor_y);
    lv_obj_set_size(r, LIST_W, ROW_H);
    lv_obj_set_style_border_width(r, list_first ? 0 : 1, 0);
    list_first = 0;
    if (item && !add_item(r, cursor_y))
        return NULL;
    cursor_y += ROW_H;
    return r;
}

/* Places o on the first line of a row, at the right or the left. The line
 * stays where it is when the row expands. */
static void first_line(lv_obj_t *o, lv_align_t align, int32_t x, int32_t h)
{
    lv_obj_align(o, align, x, (ROW_H - h) / 2);
}

/* Carbon toggle: a 48 x 24 track and an 18 px handle that is 3 px from the
 * ends */
#define TOGGLE_W  48
#define TOGGLE_H  24
#define HANDLE    18

static int32_t toggle(lv_obj_t *r, int on)
{
    lv_obj_t *track = box(r, 0, 0, TOGGLE_W, TOGGLE_H,
                          on ? COLOR_TOGGLE_ON : COLOR_TOGGLE_OFF);
    lv_obj_t *handle = box(track, on ? TOGGLE_W - 3 - HANDLE : 3, 3, HANDLE,
                           HANDLE, COLOR_ON_COLOR);

    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    first_line(track, LV_ALIGN_TOP_RIGHT, 0, TOGGLE_H);
    return TOGGLE_W;
}

/* Returns the width that the meta takes from the right of the row. */
static int32_t meta(lv_obj_t *r, enum meta_kind kind, const char *text)
{
    lv_obj_t *l;

    switch (kind) {
    case META_TEXT:
        l = ui_label(r, &sans_18, COLOR_TEXT_3, text);
        first_line(l, LV_ALIGN_TOP_RIGHT, 0, lv_font_get_line_height(&sans_18));
        return ui_text_width(&sans_18, text);
    case META_TOGGLE_ON:
    case META_TOGGLE_OFF:
        return toggle(r, kind == META_TOGGLE_ON);
    default:
        return 0;
    }
}

/* The help of an item: its text, and a warning line under it */
static void item_help(lv_obj_t *r, int32_t text_w, const char *help,
                      const char *warn)
{
    int32_t line_h = lv_font_get_line_height(&sans_18);
    /* The help starts 8 px under the title and ends as far above the
     * bottom of the row as the title is under its top. */
    int32_t y = 0, top = (ROW_H + line_h) / 2 + 8;
    lv_obj_t *box_o = lv_obj_create(r);
    lv_obj_t *l;

    lv_obj_remove_style_all(box_o);
    lv_obj_remove_flag(box_o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box_o, 0, top);
    if (help) {
        lv_point_t size;

        l = ui_label(box_o, &sans_18, COLOR_TEXT_2, help);
        lv_obj_set_width(l, text_w);
        lv_obj_set_style_text_line_space(l, LINE_SPACE, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_text_get_size(&size, help, &sans_18, 0, LINE_SPACE, text_w,
                         LV_TEXT_FLAG_NONE);
        y = size.y;
    }
    if (warn) {
        if (y)
            y += 6;
        l = ui_label(box_o, &icons_18, COLOR_WARN, ICON_WARN);
        lv_obj_set_pos(l, 0, y + 1);
        l = ui_label(box_o, &sans_18, COLOR_WARN, warn);
        lv_obj_set_pos(l, 18 + 6, y);
        y += line_h;
    }
    /* 2 px more, so that the 1 px text offset does not clip the
     * descenders of the last line */
    lv_obj_set_size(box_o, text_w, y + 2);
    lv_obj_add_flag(box_o, LV_OBJ_FLAG_HIDDEN);
    item_help_obj[item_count - 1] = box_o;
    item_extra[item_count - 1] = top + y + (ROW_H - line_h) / 2 - ROW_H;
}

lv_obj_t *ui_item(const char *title, enum meta_kind kind,
                  const char *meta_text, const char *help, const char *warn)
{
    lv_obj_t *r = row(1);
    lv_obj_t *t;
    int32_t text_w;

    if (!r)
        return NULL;
    text_w = LIST_W - 2 * PAD - meta(r, kind, meta_text);
    if (kind != META_NONE)
        text_w -= PAD;
    t = row_label(r, &sans_18, title);
    lv_obj_set_width(t, text_w);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    first_line(t, LV_ALIGN_TOP_LEFT, 0, lv_font_get_line_height(&sans_18));
    if ((help && help[0]) || warn)
        item_help(r, text_w, help && help[0] ? help : NULL, warn);
    return r;
}

lv_obj_t *ui_choice(const char *label, const char *value)
{
    lv_obj_t *r = row(1);
    lv_obj_t *l;
    int32_t w = ui_text_width(&sans_18, value);

    if (!r)
        return NULL;
    /* A label and a value, as on the Info page: the value is primary. */
    l = ui_label(r, &sans_18, COLOR_TEXT_2, label);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    l = ui_label(r, &icons_18, COLOR_TEXT_3, ICON_NEXT);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, 0, 0);
    l = row_label(r, &sans_18, value);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, -(18 + 8), 0);
    l = ui_label(r, &icons_18, COLOR_TEXT_3, ICON_PREV);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, -(18 + 8 + w + 8), 0);
    return r;
}

void ui_key_value(const char *key, const char *value)
{
    lv_obj_t *r = row(0);
    lv_obj_t *l;

    l = ui_label(r, &sans_18, COLOR_TEXT_2, key);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    l = ui_label(r, &sans_18, COLOR_TEXT, value);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void scroll_to_item(void)
{
    int32_t view = lv_obj_get_content_height(list);
    int32_t y = lv_obj_get_scroll_y(list);
    int32_t top = item_top[item] + shift(item_top[item]);
    int32_t bottom = lv_obj_get_y(items[item]) + lv_obj_get_height(items[item]);

    /* The first row also shows what is above it, for example a note. */
    if (item == 0)
        top = 0;
    if (top < y) {
        lv_obj_scroll_to_y(list, top, LV_ANIM_OFF);
    } else if (bottom > y + view) {
        /* Put the top of the list on an item, so that no item shows only
         * its lower part. */
        int32_t to = top;

        for (int i = 0; i < item_count; i++) {
            int32_t t = item_top[i] + shift(item_top[i]);

            if (t >= bottom - view && t < to)
                to = t;
        }
        lv_obj_scroll_to_y(list, to, LV_ANIM_OFF);
    }
}

static void select_item(int i)
{
    if (!item_count)
        return;
    if (i < 0)
        i = 0;
    if (i >= item_count)
        i = item_count - 1;
    if (item < item_count)
        lv_obj_remove_state(items[item], LV_STATE_FOCUSED);
    item = i;
    page_item[page] = i;
    if (in_content)
        lv_obj_add_state(items[item], LV_STATE_FOCUSED);
    /* The help shows only while the focus is on the item. */
    expand(in_content ? item : -1);
    lv_obj_update_layout(list);
    scroll_to_item();
    ui_hints_changed();
}

static void set_focus(int content)
{
    if (content && !item_count)
        return;
    in_content = content;
    update_nav();
    select_item(item);
}

void ui_rebuild(void)
{
    const struct page *p = pages[page];

    lv_obj_clean(list);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    item_count = 0;
    expanded = -1;
    cursor_y = 0;
    pending_top = -1;
    list_first = 1;
    p->build();
    /* scroll_to_item() puts an item at the top of the list. The spacer lets
     * the last items scroll that far. */
    if (cursor_y > MAIN_H) {
        lv_obj_t *spacer = box(list, 0, 0, 1, MAIN_H - ROW_H, COLOR_BG);

        place(spacer, cursor_y);
    }
    if (!item_count)
        in_content = 0;
    item = page_item[page] < item_count ? page_item[page] : 0;
    update_nav();
    select_item(item);
    ui_hints_changed();
}

static void open_page(int i)
{
    if (i < 0 || i >= PAGE_COUNT || i == page)
        return;
    page = i;
    ui_rebuild();
}

static void build_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *bar, *l;

    init_styles();
    lv_obj_remove_style_all(screen);
    lv_obj_add_style(screen, &style_plain, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    bar = box(screen, 0, 0, SCREEN_W, TOP_H, COLOR_BG);
    l = ui_label(bar, &sans_16, COLOR_TEXT, "AIPC OS");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, PAD, 0);
    top_status = box(bar, 0, 0, SCREEN_W, TOP_H, COLOR_BG);
    lv_obj_set_style_bg_opa(top_status, LV_OPA_TRANSP, 0);

    build_nav(screen);

    list = box(screen, NAV_W, MAIN_Y, SCREEN_W - NAV_W, MAIN_H, COLOR_BG);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(list, PAD + 1, 0);
    lv_obj_set_style_pad_right(list, PAD, 0);
    lv_obj_set_style_pad_top(list, 8, 0);

    bar = box(screen, 0, SCREEN_H - FOOT_H, SCREEN_W, FOOT_H, COLOR_BG);
    foot_hints = box(bar, 0, 0, SCREEN_W, FOOT_H, COLOR_BG);
    lv_obj_set_style_bg_opa(foot_hints, LV_OPA_TRANSP, 0);
    foot_status = ui_label(bar, &sans_16, COLOR_TEXT_3, "");
    /* On the top edge of the footer, where it is easy to see */
    foot_progress = box(screen, 0, SCREEN_H - FOOT_H - 2, 0, 2, COLOR_BLUE);
}

/* Autoboot */

static void autoboot_cancel(void)
{
    if (autoboot == AUTOBOOT_OFF)
        return;
    autoboot = AUTOBOOT_OFF;
    set_status(NULL);
    lv_obj_set_width(foot_progress, 0);
}

static void autoboot_tick(void)
{
    uint32_t now = timer_ms();
    uint32_t used;
    int left_s;
    int32_t w;

    if (autoboot == AUTOBOOT_WAIT_KBD) {
        if (!kbd_ready())
            return;
        autoboot = AUTOBOOT_RUN;
        autoboot_start_ms = now;
        autoboot_ms = settings_timeout() * 1000u;
        autoboot_shown_s = -1;
        autoboot_shown_w = -1;
    }
    if (autoboot != AUTOBOOT_RUN)
        return;

    used = now - autoboot_start_ms;
    if (used >= autoboot_ms) {
        int i = launch_default();

        autoboot_cancel();
        page_item[0] = i;
        if (page != 0)
            open_page(0);
        else
            select_item(i);
        launch_boot(i);
        return;
    }

    left_s = (int)((autoboot_ms - used + 999u) / 1000u);
    if (left_s != autoboot_shown_s) {
        char text[24];

        autoboot_shown_s = left_s;
        lv_snprintf(text, sizeof(text), "Auto boot in %d s", left_s);
        set_status(text);
    }
    w = (int32_t)((uint64_t)(autoboot_ms - used) * SCREEN_W / autoboot_ms);
    if (w != autoboot_shown_w) {
        autoboot_shown_w = w;
        lv_obj_set_width(foot_progress, w);
    }
}

/* Theme */

/* Builds the screen again when the theme in the settings changed. The
 * objects hold their colors, so a new palette needs new objects. */
static void theme_sync(void)
{
    const struct theme *t = settings_dark() ? &dark : &light;

    if (t == ui_theme)
        return;
    ui_theme = t;
    modal_close();
    lv_obj_clean(lv_screen_active());
    build_screen();
    autoboot_shown_s = -1;
    autoboot_shown_w = -1;
}

void ui_apply_theme(void)
{
    theme_sync();
    update_top_bar();
    ui_rebuild();
}

/* Card */

static void reload(int startup)
{
    const char *msg = NULL;

    card.space_ok = 0;
    if (card_state() == CARD_READY) {
        settings_load();
        msg = entries_load();
        payload_list();
        card.space_ok = !storage_space(&card.free_mb, &card.total_mb,
                                       &card.type);
    } else {
        /* The settings stay as they are, so that a change without a card
         * keeps working until a card goes in. */
        entry_count = 0;
        payload_count = 0;
    }
    lcd_backlight(settings_backlight());
    theme_sync();
    launch_reload();
    update_top_bar();

    if (msg) {
        modal_error("entries.ini", "Problem in entries.ini", msg);
    } else if (startup && card_state() == CARD_READY) {
        autoboot = AUTOBOOT_WAIT_KBD;
    }
}

void ui_card_changed(void)
{
    char keep[48] = "";

    if (page == 0 && item < item_count)
        strcpy(keep, launch_title(item));
    autoboot_cancel();
    if (modal_is_open())
        modal_close();
    reload(0);
    if (keep[0]) {
        int i = launch_find(keep);

        page_item[0] = i >= 0 ? i : 0;
    }
    ui_rebuild();
}

/* Battery */

static void battery_tick(int force)
{
    uint32_t now = timer_ms();
    int mv;

    if (!force && now - battery_ms < BATTERY_POLL_MS)
        return;
    battery_ms = now;
    mv = battery_mv();
    /* The bar shows 10 mV steps. */
    if (mv / 10 == battery / 10)
        return;
    battery = mv;
    update_top_bar();
    if (pages[page] == &page_info)
        ui_rebuild();
}

/* Power key */

static int power_armed;

void ui_power_key_rearm(void)
{
    power_armed = 0;
}

static void power_key_click(void)
{
    int power = PAGE_COUNT - 1;

    if (page == power)
        return;
    modal_close();
    page = power;
    page_item[power] = 0;
    in_content = 1;
    ui_rebuild();
}

static void power_key_tick(void)
{
    static const struct modal hold = {
        .title = "Keep holding to power off",
        .progress = 1,
    };
    static int down, shown;
    static uint32_t down_ms;
    int key = power_key_down();
    uint32_t held;

    /* The press that turned the device on can still be down. */
    if (!power_armed) {
        power_armed = !key;
        return;
    }
    if (!key) {
        if (down && shown)
            modal_close();
        else if (down)
            power_key_click();
        down = shown = 0;
        return;
    }
    if (!down) {
        down = 1;
        down_ms = timer_ms();
        autoboot_cancel();
        return;
    }
    held = timer_ms() - down_ms;
    if (held >= KEY_OFF_MS)
        power_off();
    if (held >= KEY_CLICK_MS) {
        if (!shown)
            modal_open(&hold);
        shown = 1;
        modal_progress((held - KEY_CLICK_MS) * 1000u /
                       (KEY_OFF_MS - KEY_CLICK_MS));
    }
}

/* Keys. The focus moves by rules that LVGL groups do not have, so the menu
 * has no LVGL input device. */

static void content_key(uint8_t key)
{
    const struct page *p = pages[page];

    switch (key) {
    case KBD_UP:
        if (item > 0)
            select_item(item - 1);
        break;
    case KBD_DOWN:
        if (item + 1 < item_count)
            select_item(item + 1);
        break;
    case KBD_LEFT:
    case KBD_RIGHT: {
        int dir = key == KBD_LEFT ? -1 : 1;

        if (p->side && p->side(item, dir))
            ui_hints_changed();
        else if (dir < 0)
            set_focus(0);
        break;
    }
    case KBD_HOME:
        select_item(0);
        break;
    case KBD_END:
        select_item(item_count - 1);
        break;
    case KBD_ENTER:
        if (p->enter)
            p->enter(item);
        break;
    case KBD_ESC:
        set_focus(0);
        break;
    }
}

static void nav_key(uint8_t key)
{
    switch (key) {
    case KBD_UP:
        open_page(page - 1);
        break;
    case KBD_DOWN:
        open_page(page + 1);
        break;
    case KBD_HOME:
        open_page(0);
        break;
    case KBD_END:
        open_page(PAGE_COUNT - 1);
        break;
    case KBD_RIGHT:
    case KBD_ENTER:
        set_focus(1);
        break;
    }
}

static void poll_keyboard(void)
{
    static int was_ready = -1;
    uint8_t key;
    int pressed;

    kbd_poll();
    if (kbd_ready() != was_ready) {
        was_ready = kbd_ready();
        ui_hints_changed();
    }
    while (kbd_pop(&key, &pressed)) {
        if (!pressed)
            continue;
        autoboot_cancel();
        if (modal_is_open())
            modal_key(key);
        else if (in_content)
            content_key(key);
        else
            nav_key(key);
    }
}

void ui_run(void)
{
    lv_init();
    lv_tick_set_cb(timer_ms);
    lv_delay_set_cb(timer_delay_ms);

    lv_display_t *disp = lv_display_create(FB_WIDTH, FB_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, (void *)(uintptr_t)FB_ADDR,
                           (void *)(uintptr_t)FB_BACK_ADDR, FB_BYTES,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    build_screen();
    battery_tick(1);
    reload(1);
    page_item[0] = launch_default();
    in_content = 1;
    ui_rebuild();

    for (;;) {
        poll_keyboard();
        power_key_tick();
        battery_tick(0);
        if (card_poll())
            ui_card_changed();
        autoboot_tick();
        lv_timer_handler();
    }
}
