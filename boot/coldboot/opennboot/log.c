#include "log.h"
#include "soc.h"

static volatile char *log_ptr;
static volatile char *log_end;

void log_init(void)
{
    volatile char *p = (volatile char *)(uintptr_t)LOG_BASE;
    volatile char *e = p + LOG_SIZE;
    for (volatile char *q = p; q < e; q++)
        *q = 0;
    log_ptr = p;
    log_end = e;
}

void log_putc(char c)
{
    if (log_ptr < log_end)
        *log_ptr++ = c;
    uart_putc(c);
}

void log_puts(const char *s)
{
    for (; *s; ++s)
        log_putc(*s);
}

void log_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    log_putc('0');
    log_putc('x');
    for (int i = 7; i >= 0; --i)
        log_putc(hex[(v >> (i * 4)) & 0xFu]);
}

/* No libgcc, so no __aeabi_uidivmod. */
void log_dec(uint32_t v)
{
    static const uint32_t pow10[10] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    int started = 0;

    for (int i = 0; i < 10; i++) {
        char d = '0';
        while (v >= pow10[i]) {
            v -= pow10[i];
            d++;
        }
        if (d != '0' || started || i == 9) {
            log_putc(d);
            started = 1;
        }
    }
}

void log_rc(int rc)
{
    log_puts(" rc=");
    log_dec((uint32_t)(-rc));
    log_putc('\n');
}
