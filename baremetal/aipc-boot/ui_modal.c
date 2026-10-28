#include "kbd.h"
#include "ui_priv.h"

#define PANEL_W       480
#define BUTTON_H      64

static lv_obj_t *overlay;
static lv_obj_t *buttons[2];
static lv_obj_t *bar;
static struct modal cur;
static int focus;             /* 0: cancel, 1: ok */

int modal_is_open(void)
{
    return overlay != NULL;
}

static uint32_t button_color(int i)
{
    if (i == 0)
        return COLOR_BUTTON_2;
    if (cur.danger)
        return COLOR_DANGER;
    return COLOR_BLUE;
}

static void style_buttons(void)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = buttons[i];

        if (!b)
            continue;
        lv_obj_set_style_bg_color(b, lv_color_hex(button_color(i)), 0);
        lv_obj_set_style_outline_width(b, i == focus ? 2 : 0, 0);
    }
}

static lv_obj_t *button(lv_obj_t *panel, int i, int32_t x, int32_t w,
                        const char *text)
{
    lv_obj_t *b = lv_obj_create(panel);
    lv_obj_t *l;

    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, BUTTON_H);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, x, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_color(b, lv_color_hex(COLOR_ON_COLOR), 0);
    lv_obj_set_style_outline_pad(b, -2, 0);
    l = ui_label(b, &sans_18, COLOR_ON_COLOR, text);
    lv_obj_set_pos(l, PAD, PAD);
    buttons[i] = b;
    return b;
}

void modal_open(const struct modal *m)
{
    lv_obj_t *panel, *l;
    int32_t y = PAD, text_w = PANEL_W * 8 / 10;
    lv_point_t size;

    if (overlay)
        modal_close();
    cur = *m;
    focus = m->cancel ? 0 : 1;

    overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(overlay, 0, MAIN_Y);
    lv_obj_set_size(overlay, SCREEN_W, MAIN_H);
    /* A translucent overlay must read back the uncached framebuffer under
     * it. */
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(COLOR_SCRIM), 0);

    panel = lv_obj_create(overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);

    if (m->label) {
        l = ui_label(panel, &sans_16, COLOR_TEXT_2, m->label);
        lv_obj_set_pos(l, PAD, y);
        y += 22;
    }
    l = ui_label(panel, &sans_22, COLOR_TEXT, m->title);
    lv_obj_set_width(l, PANEL_W - 2 * PAD);
    lv_obj_set_style_text_line_space(l, LINE_SPACE, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(l, PAD, y);
    lv_text_get_size(&size, m->title, &sans_22, 0, LINE_SPACE,
                     PANEL_W - 2 * PAD, LV_TEXT_FLAG_NONE);
    y += size.y;
    if (m->body) {
        y += PAD;
        l = ui_label(panel, &sans_18, COLOR_TEXT, m->body);
        lv_obj_set_width(l, text_w);
        lv_obj_set_style_text_line_space(l, LINE_SPACE, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(l, PAD, y);
        lv_text_get_size(&size, m->body, &sans_18, 0, LINE_SPACE, text_w,
                         LV_TEXT_FLAG_NONE);
        y += size.y;
    }
    y += 3 * PAD;

    buttons[0] = buttons[1] = NULL;
    bar = NULL;
    if (m->progress) {
        lv_obj_set_size(panel, PANEL_W, y);
        bar = lv_obj_create(panel);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 0, 4);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_BLUE), 0);
    } else {
        lv_obj_set_size(panel, PANEL_W, y + BUTTON_H);
        if (m->cancel)
            button(panel, 0, 0, PANEL_W / 2, m->cancel);
        button(panel, 1, PANEL_W / 2, PANEL_W / 2, m->ok);
        style_buttons();
    }
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    ui_hints_changed();
}

void modal_close(void)
{
    if (!overlay)
        return;
    lv_obj_delete(overlay);
    overlay = NULL;
    buttons[0] = buttons[1] = NULL;
    bar = NULL;
    ui_hints_changed();
}

static void finish(int ok)
{
    void (*done)(int) = cur.done;

    modal_close();
    if (done)
        done(ok);
}

void modal_progress(uint32_t permille)
{
    if (bar)
        lv_obj_set_width(bar, (int32_t)(PANEL_W * permille / 1000u));
}

void modal_key(uint8_t key)
{
    if (cur.progress)
        return;
    switch (key) {
    case KBD_LEFT:
    case KBD_RIGHT:
        if (cur.cancel) {
            focus = key == KBD_RIGHT;
            style_buttons();
            ui_hints_changed();
        }
        break;
    case KBD_ENTER:
        finish(focus);
        break;
    case KBD_ESC:
        finish(!cur.cancel);
        break;
    }
}

void modal_hints(const char **enter, const char **esc, int *choose)
{
    *enter = cur.progress ? NULL : focus ? cur.ok : cur.cancel;
    *esc = cur.cancel;
    *choose = cur.cancel != NULL;
}

void modal_error(const char *label, const char *title, const char *body)
{
    struct modal m = {
        .label = label,
        .title = title,
        .body = body,
        .ok = "OK",
    };

    modal_open(&m);
}
