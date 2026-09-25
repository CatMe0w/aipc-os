#include <stdarg.h>
#include "fbcon.h"
#include "log.h"
#include "nkpf.h" /* IWYU pragma: keep */

static void out(char c)
{
    log_putc(c);
    screen_putc((uint8_t)c);
}

static void out_num(uint32_t v, unsigned base, unsigned width, char pad)
{
    char buf[12];
    unsigned n = 0;

    do {
        unsigned d = v % base;

        buf[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v /= base;
    } while (v);
    while (n < width--)
        out(pad);
    while (n)
        out(buf[--n]);
}

static void vprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        unsigned width = 0;
        char pad = ' ';

        if (*fmt != '%') {
            out(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == '0')
            pad = '0';
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (unsigned)(*fmt++ - '0');
        switch (*fmt) {
        case 'c': out((char)va_arg(ap, int)); break;
        case 's': for (const char *s = va_arg(ap, const char *); *s; s++) out(*s); break;
        case 'u': out_num(va_arg(ap, uint32_t), 10, width, pad); break;
        case 'x':
        case 'X': out_num(va_arg(ap, uint32_t), 16, width, pad); break;
        case 'p': out('0'); out('x'); out_num(va_arg(ap, uint32_t), 16, 8, '0'); break;
        case '%': out('%'); break;
        default: out('?'); break;
        }
    }
}

void printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...)
{
    va_list ap;

    screen_set_color(RGB565_RED);
    printf("\npanic: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\nSystem halted.");
    for (;;)
        ;
}
