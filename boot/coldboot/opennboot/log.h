#pragma once
#include <stdint.h>

/* In the gap between EBOOT's image (~0x30110000) and the WinCE kernel load
 * address (0x30200000), so the log survives a WinCE handoff. */
#define LOG_BASE  0x301F0000u
#define LOG_SIZE  0x00010000u

void log_init(void);
void log_putc(char c);
void log_puts(const char *s);
void log_hex32(uint32_t v);
void log_dec(uint32_t v);

/* " rc=N\n", for the negative codes every module returns. */
void log_rc(int rc);
