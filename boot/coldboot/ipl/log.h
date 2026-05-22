#ifndef IPL_LOG_H
#define IPL_LOG_H

#include <stdint.h>

/* Memory-backed log, read back via cold-boot dump.
 *
 * Buffer lives at 0x31D00000 (64 KB), matching the convention used by
 * doom/src/syscalls.c. UART hardware on the dev device is dead, so all
 * diagnostic output goes here. log_init zeroes the whole buffer; the dump
 * reader trims at the first NUL byte to recover the text. */

#define IPL_LOG_BASE 0x31D00000u
#define IPL_LOG_SIZE 0x00010000u

void log_init(void);
void log_putc(char c);
void log_puts(const char *s);
void log_put_hex32(uint32_t v);

#endif
