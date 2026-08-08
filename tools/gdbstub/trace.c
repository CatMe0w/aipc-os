#include "trace.h"

static char *g_put;
static char *g_end;

/* Preserve the previous run's log, then start fresh. */
void trace_init(void)
{
    volatile uint32_t *live = (volatile uint32_t *)TRACE_BASE;
    volatile uint32_t *prev = (volatile uint32_t *)TRACE_PREV;
    uint32_t i;

    for (i = 0; i < TRACE_SIZE / 4; i++) {
        prev[i] = live[i];
        live[i] = 0;
    }

    g_put = (char *)TRACE_BASE;
    g_end = (char *)(TRACE_BASE + TRACE_SIZE - 1);
}

static void trace_putc(char c)
{
    if (g_put < g_end)
        *g_put++ = c;
}

void trace_puts(const char *s)
{
    while (*s)
        trace_putc(*s++);
}

void trace_hex(uint32_t value, uint32_t digits)
{
    static const char digit[] = "0123456789ABCDEF";
    uint32_t i;

    for (i = digits; i > 0; i--)
        trace_putc(digit[(value >> ((i - 1) * 4)) & 0xF]);
}

void trace_dec(uint32_t value)
{
    static const uint32_t pow10[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    uint32_t i;
    uint32_t started = 0;

    for (i = 0; i < 10; i++) {
        uint32_t n = 0;

        while (value >= pow10[i]) {
            value -= pow10[i];
            n++;
        }
        if (n || started || i == 9) {
            trace_putc((char)('0' + n));
            started = 1;
        }
    }
}

uint32_t trace_used(void)
{
    return (uint32_t)(g_put - (char *)TRACE_BASE);
}

/* Word at a time to avoid per-byte bus transactions with the MMU off */
uint32_t trace_prev_used(void)
{
    const volatile uint32_t *p = (const volatile uint32_t *)TRACE_PREV;
    uint32_t i;

    for (i = 0; i < TRACE_SIZE / 4; i++) {
        uint32_t w = p[i];

        if (!(w & 0x000000FFu))
            return i * 4;
        if (!(w & 0x0000FF00u))
            return i * 4 + 1;
        if (!(w & 0x00FF0000u))
            return i * 4 + 2;
        if (!(w & 0xFF000000u))
            return i * 4 + 3;
    }
    return TRACE_SIZE;
}

void trace_reg(const char *name, uint32_t value)
{
    trace_puts(name);
    trace_puts("=0x");
    trace_hex(value, 8);
    trace_putc('\n');
}
