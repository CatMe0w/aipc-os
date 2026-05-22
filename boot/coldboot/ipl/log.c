#include "log.h"

static volatile char *log_ptr;
static volatile char *log_end;

void log_init(void)
{
    volatile char *p = (volatile char *)IPL_LOG_BASE;
    volatile char *e = p + IPL_LOG_SIZE;
    for (volatile char *q = p; q < e; q++)
        *q = 0;
    log_ptr = p;
    log_end = e;
}

void log_putc(char c)
{
    if (log_ptr < log_end)
        *log_ptr++ = c;
}

void log_puts(const char *s)
{
    for (; *s; ++s)
        log_putc(*s);
}

void log_put_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    log_putc('0');
    log_putc('x');
    for (int i = 7; i >= 0; --i)
        log_putc(hex[(v >> (i * 4)) & 0xFu]);
}
