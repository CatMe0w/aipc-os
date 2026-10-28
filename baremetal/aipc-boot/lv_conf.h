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

/* The fonts are in fonts/. */
#define LV_FONT_MONTSERRAT_14       0
#define LV_FONT_CUSTOM_DECLARE      LV_FONT_DECLARE(sans_16)
#define LV_FONT_DEFAULT             &sans_16
#define LV_USE_FONT_COMPRESSED      1

/* The UI draws RGB565 into the framebuffer, and glyphs as A8. Complex
 * drawing is for the round toggles, and it costs about 13 KB. */
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED          0
#define LV_DRAW_SW_SUPPORT_RGB565A8                0
#define LV_DRAW_SW_SUPPORT_RGB888                  0
#define LV_DRAW_SW_SUPPORT_XRGB8888                0
#define LV_DRAW_SW_SUPPORT_ARGB8888                0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED  0
#define LV_DRAW_SW_SUPPORT_L8                      0
#define LV_DRAW_SW_SUPPORT_AL88                    0
#define LV_DRAW_SW_SUPPORT_I1                      0
#define LV_DRAW_SW_COMPLEX                         1

/* The UI sets every style and position itself. */
#define LV_USE_THEME_DEFAULT        0
#define LV_USE_THEME_SIMPLE         0
#define LV_USE_THEME_MONO           0
#define LV_USE_FLEX                 0
#define LV_USE_GRID                 0

/* The UI uses only lv_obj and lv_label. */
#define LV_USE_ARCLABEL             0
#define LV_USE_BAR                  0
#define LV_USE_BUTTON               0
#define LV_USE_BUTTONMATRIX         0
#define LV_USE_IMAGE                0
#define LV_USE_IMAGEBUTTON          0
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
