/* panic() for lib/pf. It supports only %s and %u, which are the only
 * conversions that lib/pf and eboot_patch use. */

#include <stdarg.h>
#include "log.h"
#include "pf.h"

void panic(const char *fmt, ...)
{
    va_list ap;

    log_puts("panic: ");
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (fmt[0] == '%' && fmt[1] == 's') {
            log_puts(va_arg(ap, const char *));
            fmt++;
        } else if (fmt[0] == '%' && fmt[1] == 'u') {
            log_dec(va_arg(ap, unsigned));
            fmt++;
        } else {
            log_putc(*fmt);
        }
    }
    va_end(ap);
    log_puts("\nsystem halted");
    for (;;)
        ;
}
