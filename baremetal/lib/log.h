#pragma once
#include <stdint.h>

/* This window sits in the gap between the EBOOT image (~0x30110000) and the
 * WinCE kernel load address (0x30200000), so the log survives a WinCE handoff.
 * A payload that runs elsewhere in DDR overrides LOG_BASE. */
#ifndef LOG_BASE
#define LOG_BASE  0x301F0000u
#endif
#define LOG_SIZE  0x00010000u

void log_init(void);

/* Give up the DDR half of the log and keep only the UART. Without this call,
 * the lines logged after a load corrupt any payload that covers the window. */
void log_detach(void);

void log_putc(char c);
void log_puts(const char *s);
void log_hex32(uint32_t v);
void log_dec(uint32_t v);

/* " rc=N\n", for the negative codes every module returns. */
void log_rc(int rc);
