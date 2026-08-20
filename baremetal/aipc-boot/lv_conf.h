/*
 * LVGL configuration for aipc-boot.
 *
 * Only the options that differ from LVGL's defaults appear here, everything
 * else falls back to src/lv_conf_internal.h.
 *
 * The framebuffer is the render target, so LVGL never needs a second buffer.
 * There is no OS, no filesystem and no libc heap for LVGL: the built-in
 * allocator works out of a static pool inside our .bss.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH              16

#define LV_USE_STDLIB_MALLOC        LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING        LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF       LV_STDLIB_BUILTIN
#define LV_MEM_SIZE                 (512 * 1024U)

#define LV_DEF_REFR_PERIOD          33
#define LV_DPI_DEF                  130
#define LV_USE_OS                   LV_OS_NONE
#define LV_USE_LOG                  0

#define LV_FONT_MONTSERRAT_16       1
#define LV_FONT_MONTSERRAT_22       1
#define LV_FONT_MONTSERRAT_34       1
#define LV_FONT_DEFAULT             &lv_font_montserrat_16

#define LV_USE_THEME_DEFAULT        1
#define LV_THEME_DEFAULT_DARK       1
#define LV_THEME_DEFAULT_GROW       0

/* Nothing in the menu needs these, and they cost image size. */
#define LV_USE_ANIMIMG              0
#define LV_USE_ARC                  0
#define LV_USE_CALENDAR             0
#define LV_USE_CANVAS               0
#define LV_USE_CHART                0
#define LV_USE_CHECKBOX             0
#define LV_USE_DROPDOWN             0
#define LV_USE_KEYBOARD             0
#define LV_USE_LED                  0
#define LV_USE_LINE                 0
#define LV_USE_LIST                 0
#define LV_USE_MENU                 0
#define LV_USE_MSGBOX               0
#define LV_USE_ROLLER               0
#define LV_USE_SCALE                0
#define LV_USE_SLIDER               0
#define LV_USE_SPAN                 0
#define LV_USE_SPINBOX              0
#define LV_USE_SPINNER              0
#define LV_USE_SWITCH               0
#define LV_USE_TABLE                0
#define LV_USE_TABVIEW              0
#define LV_USE_TEXTAREA             0
#define LV_USE_TILEVIEW             0
#define LV_USE_WIN                  0

#endif /* LV_CONF_H */
