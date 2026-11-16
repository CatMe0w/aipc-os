#include <stddef.h>
#include <stdint.h>

#include "ch374.h"
#include "log.h"
#include "timer.h"

#define KEY_BS   0x08
#define KEY_CR   0x0D
#define KEY_ESC  0x1B

#define USAGE_LSHIFT  0xE1u
#define USAGE_RSHIFT  0xE5u

/* The console_putchar of EBOOT. The eboot_menu component fills it in. */
__attribute__((used)) void (*eboot_menu_putc)(int c);

static int started;
static int shift;

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;

    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}

/* A log line from the CH374 driver would break the countdown line of EBOOT */
void log_puts(const char *s)
{
}

static void put_char(char c)
{
    eboot_menu_putc((uint8_t)c);
}

static void put_string(const char *s)
{
    while (*s)
        put_char(*s++);
}

static uint8_t usage_to_ascii(uint8_t usage)
{
    if (usage >= 0x04 && usage <= 0x1D)
        return (uint8_t)((shift ? 'A' : 'a') + usage - 0x04);
    if (usage >= 0x1E && usage <= 0x26)
        return (uint8_t)('1' + usage - 0x1E);
    if (usage >= 0x59 && usage <= 0x61)     /* keypad 1 to 9 */
        return (uint8_t)('1' + usage - 0x59);

    switch (usage) {
    case 0x27: case 0x62: return '0';
    case 0x28: case 0x58: return KEY_CR;
    case 0x29: return KEY_ESC;
    case 0x2A: return KEY_BS;
    case 0x2C: return ' ';
    case 0x2D: return '-';
    case 0x37: case 0x63: return '.';
    default:   return 0;
    }
}

/* EBOOT sets up its own pins before the menu, so the keyboard starts at the
 * first call from EBOOT. */
static void start(void)
{
    if (started)
        return;
    started = 1;
    timer_init();
    ch374_kbd_init();
}

/* The clock of the countdown: seconds since the first call into the
 * resident. */
uint32_t eboot_menu_secs(void)
{
    start();
    return timer_ms() / 1000u;
}

/* OEMReadDebugByte, which the stock EBOOT does not implement: the next key,
 * or 0 when there is none. It does not wait. */
int eboot_menu_read_byte(void)
{
    uint8_t usage;
    int pressed;

    start();
    ch374_kbd_poll();
    while (ch374_kbd_pop(&usage, &pressed)) {
        uint8_t c;

        if (usage == USAGE_LSHIFT || usage == USAGE_RSHIFT) {
            shift = pressed;
            continue;
        }
        if (!pressed)
            continue;
        c = usage_to_ascii(usage);
        if (c != 0)
            return c;
    }
    return 0;
}

static int wait_byte(void)
{
    int c;

    while ((c = eboot_menu_read_byte()) == 0)
        ;
    return c;
}

/* The line input of the menu. Returns the length with the terminator,
 * so 1 is an empty line. */
int eboot_menu_get_string(char *buf, int size)
{
    int n = 0;

    if (buf == NULL)
        return 0;

    for (;;) {
        int c = wait_byte();

        if (c == KEY_BS) {
            if (n > 0) {
                put_string("\b \b");
                n--;
            }
        } else if (c == KEY_CR || n >= size - 1) {
            put_string("\r\n");
            break;
        } else {
            put_char((char)c);
            buf[n++] = (char)c;
        }
    }
    buf[n++] = '\0';
    return n;
}

/* The character input of the menu */
int eboot_menu_get_char(void)
{
    int c = wait_byte();

    put_char((char)c);
    return c;
}

static uint32_t parse_uint(const char *s)
{
    uint32_t base = 10;
    uint32_t v = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (;; s++) {
        uint32_t d;

        if (*s >= '0' && *s <= '9')
            d = (uint32_t)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = (uint32_t)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = (uint32_t)(*s - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    return v;
}

/* The integer input of the menu: decimal, or hexadecimal after "0x".
 * Returns 0 for an empty line, and *value then stays as it is. */
int eboot_menu_get_integer(uint32_t *value)
{
    char buf[16];

    if (eboot_menu_get_string(buf, sizeof(buf)) == 1)
        return 0;
    *value = parse_uint(buf);
    return 1;
}

/* The y/n prompt of the menu. Enter gives the default answer. */
int eboot_menu_ask_yes_no(const char *question, int default_yes)
{
    put_string(question);
    put_string("\r\nyes or no [");
    put_char(default_yes ? 'y' : 'n');
    put_string("]: ");

    for (;;) {
        int c = wait_byte();

        if (c == KEY_CR)
            c = default_yes ? 'y' : 'n';
        if (c == 'y' || c == 'Y') {
            put_string("y\r\n");
            return 1;
        }
        if (c == 'n' || c == 'N') {
            put_string("n\r\n");
            return 0;
        }
    }
}
