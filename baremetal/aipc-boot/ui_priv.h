#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lvgl/lvgl.h"

LV_FONT_DECLARE(sans_16);
LV_FONT_DECLARE(sans_18);
LV_FONT_DECLARE(sans_22);
LV_FONT_DECLARE(icons_18);
LV_FONT_DECLARE(icons_20);

#define ICON_LAUNCH    "\xEE\xB5\x86"  /* U+ED46 player-play */
#define ICON_PAYLOADS  "\xEE\xAB\xBF"  /* U+EAFF package */
#define ICON_CFW       "\xEE\xAC\x90"  /* U+EB10 puzzle */
#define ICON_TOOLS     "\xEE\xAD\x80"  /* U+EB40 tool */
#define ICON_INFO      "\xEE\xAB\x85"  /* U+EAC5 info-circle */
#define ICON_SETTINGS  "\xEE\xAC\xA0"  /* U+EB20 settings */
#define ICON_POWER     "\xEE\xAC\x8D"  /* U+EB0D power */
#define ICON_SD        "\xEF\x8E\x84"  /* U+F384 device-sd-card */
#define ICON_BATTERY   "\xEE\xA8\xB4"  /* U+EA34 battery */
#define ICON_PREV      "\xEE\xA9\xA0"  /* U+EA60 chevron-left */
#define ICON_NEXT      "\xEE\xA9\xA1"  /* U+EA61 chevron-right */
#define ICON_WARN      "\xEE\xA8\x86"  /* U+EA06 alert-triangle */

#define ARROW_LEFT     "\xE2\x86\x90"
#define ARROW_UP       "\xE2\x86\x91"
#define ARROW_RIGHT    "\xE2\x86\x92"
#define ARROW_DOWN     "\xE2\x86\x93"

/* The colors of a theme. Each gray is exact in RGB565. */
struct theme {
    uint32_t bg;
    uint32_t fill;        /* focus fill */
    uint32_t divider;     /* dividers, keycaps */
    uint32_t panel;       /* modal */
    uint32_t scrim;       /* under a modal */
    uint32_t text;        /* items, values */
    uint32_t text_2;      /* keys, help text, idle pages */
    uint32_t text_3;      /* list headers, status, hints, metas */
    uint32_t focus;       /* focus ring */
    uint32_t warn;
    uint32_t button_2;    /* secondary button */
    uint32_t toggle_off;
    uint32_t toggle_on;
};

/* The theme that settings_dark() selects. ui_apply_theme() sets it. */
extern const struct theme *ui_theme;

#define COLOR_BG         (ui_theme->bg)
#define COLOR_FILL       (ui_theme->fill)
#define COLOR_DIVIDER    (ui_theme->divider)
#define COLOR_PANEL      (ui_theme->panel)
#define COLOR_SCRIM      (ui_theme->scrim)
#define COLOR_TEXT       (ui_theme->text)
#define COLOR_TEXT_2     (ui_theme->text_2)
#define COLOR_TEXT_3     (ui_theme->text_3)
#define COLOR_FOCUS      (ui_theme->focus)
#define COLOR_WARN       (ui_theme->warn)
#define COLOR_BUTTON_2   (ui_theme->button_2)
#define COLOR_TOGGLE_OFF (ui_theme->toggle_off)
#define COLOR_TOGGLE_ON  (ui_theme->toggle_on)
#define COLOR_ON_COLOR  0xFFFFFF   /* text on a blue or red fill */
#define COLOR_BLUE      0x0F62FE
#define COLOR_DANGER    0xDA1E28

#define SCREEN_W   800
#define SCREEN_H   480
#define TOP_H      36
#define FOOT_H     36
#define NAV_W      176
#define PAD        16
/* Extra space between the lines of wrapped text */
#define LINE_SPACE 6
#define MAIN_Y     TOP_H
#define MAIN_H     (SCREEN_H - TOP_H - FOOT_H)
/* The width of a list row, inside the side margins */
#define LIST_W     (SCREEN_W - NAV_W - 1 - 2 * PAD)
#define ROW_H      48

/*
 * A page in the side navigation. build() adds the items with ui_item() or
 * ui_choice(). Up and Down move the focus between the items.
 */
struct page {
    const char *name;
    const char *icon;
    void (*build)(void);
    void (*enter)(int item);              /* optional */
    /* Optional. dir is -1 or 1. Returns 0 when the key goes back to the
     * navigation. */
    int (*side)(int item, int dir);
    /* The action of Enter, and of Left and Right, for the item. NULL hides
     * the hint. */
    void (*hints)(int item, const char **enter, const char **side);
};

extern const struct page page_launch, page_payloads, page_cfw, page_tools,
    page_info, page_settings, page_power;

/* ui.c */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                   const char *text);
int32_t ui_text_width(const lv_font_t *font, const char *text);

/* A list header. The items after it form one list. */
void ui_section(const char *title);
/* A line of text in place of the items, for example without a card */
void ui_note(const char *text);

/* What the right side of an item shows */
enum meta_kind {
    META_NONE,
    META_TEXT,      /* for example "Default" */
    META_TOGGLE_ON,
    META_TOGGLE_OFF,
};

/* An item: a title, and a meta on the right. While the item has focus, it
 * shows help and warn under the title. */
lv_obj_t *ui_item(const char *title, enum meta_kind kind, const char *meta,
                  const char *help, const char *warn);
/* An item whose value Left and Right change */
lv_obj_t *ui_choice(const char *label, const char *value);
/* A read-only row with a key and a value. It is not an item. */
void ui_key_value(const char *key, const char *value);

void ui_rebuild(void);
/* Builds the whole screen again in the theme that settings_dark() names. */
void ui_apply_theme(void);
void ui_hints_changed(void);
/* ui_dark() fades the screen out, turns the backlight off and stops scan
 * out, so a load gets the whole DDR bandwidth. The next stage sets up the
 * LCD itself. ui_light() draws the menu again and turns the screen on after
 * a failed load. */
void ui_dark(void);
void ui_light(void);
/* Draws the menu again after a payload returns. */
void ui_resume(void);

/* Ignores the power key until it is released. */
void ui_power_key_rearm(void);

/* Reads the card again after a change. */
void ui_card_changed(void);

/* The mounted card, read when it goes in */
struct ui_card {
    int space_ok;
    uint32_t free_mb;
    uint32_t total_mb;
    const char *type;
};

const struct ui_card *ui_card(void);
void ui_size_text(char *buf, size_t n, uint32_t mb);
void ui_battery_text(char *buf, size_t n);
/* The last battery reading in mV, or a negative code */
int ui_battery_mv(void);

/* ui_modal.c */
struct modal {
    const char *label;    /* optional context above the title */
    const char *title;
    const char *body;     /* optional */
    const char *cancel;   /* NULL for a single button */
    const char *ok;
    int danger;           /* the ok button erases or writes something */
    int progress;         /* no buttons, a progress bar */
    void (*done)(int ok); /* optional */
};

void modal_open(const struct modal *m);
void modal_close(void);
int modal_is_open(void);
void modal_key(uint8_t key);
/* Sets the bar of a progress modal, 0 to 1000. */
void modal_progress(uint32_t permille);
/* The footer hints while the modal is open. esc is NULL for a single
 * button, because Esc and Enter then do the same. */
void modal_hints(const char **enter, const char **esc, int *choose);
/* A single-button error modal. */
void modal_error(const char *label, const char *title, const char *body);

/* ui_launch.c */
void launch_reload(void);
int launch_count(void);
const char *launch_title(int i);
int launch_find(const char *title);
/* The item that autoboot starts. */
int launch_default(void);
void launch_boot(int i);

#include "fade.h"
