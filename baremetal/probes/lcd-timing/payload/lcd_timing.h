#pragma once
#include <stdint.h>

#define LCD_RESULT_BASE   0x32008000u

#define PAINT_ROWS        1440u

void lcd_timing_run(void);
