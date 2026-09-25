#include "fbcon.h"
#include "layout.h"

#define FB            ((volatile uint16_t *)(FB_PHYS + NK_UNCACHED_OFFSET))
#define DRAW_EVERY    512u
#define IOCTL_LOG     6u

/* The widest row is `ioctl` with two 10-digit counts. */
#define PANEL_COLS    36u
#define PANEL_ROWS    (4u + IOCTL_LOG)
#define PANEL_WIDTH   (PANEL_COLS * 8u + 8u)
#define PANEL_HEIGHT  (PANEL_ROWS * 9u)
#define LABEL_COLS    8u

/* KData.pCurPrc. The first byte of a PROCESS is its slot number. */
#define CUR_PROC      (*(volatile uint8_t *const *)0xFFFFC890u)

/* CH374.dll sets its SPI chip select through a GPIO IOCTL on every transfer,
 * and its poll thread never stops. These calls are only counted, because they
 * would fill the log. */
#define CH374_CS_PIN  0x54u

enum { ARG_NONE, ARG_OK, ARG_UNREADABLE };

struct ioctl_entry {
    uint32_t code;
    uint32_t arg[2];
    uint8_t arg_state[2];
    uint8_t slot;           /* 0xFF if unreadable */
};

static uint32_t irqs, last_pc;
static uint32_t seen_lo, seen_hi;  /* since boot */
static uint32_t win_lo, win_hi;    /* since the last draw */
static uint32_t ioctls, ch374_ioctls;
static struct ioctl_entry ioctl_log[IOCTL_LOG];
static int screen_ready;
static uint32_t col;

/* A data abort in these hooks stops the kernel. They therefore only read the
 * slotted user space and the static DDR mapping. */
static int readable(const void *p)
{
    uintptr_t a = (uintptr_t)p;

    if (a & 3u)
        return 0;
    return (a >= 0x00010000u && a < 0x42000000u) ||
           (a >= 0x80000000u && a < 0x84000000u);
}

static void put_c(char c)
{
    screen_putc((uint8_t)c);
    col++;
}

static void put_s(const char *s)
{
    while (*s)
        put_c(*s++);
}

static void pad_to(uint32_t n)
{
    while (col < n)
        put_c(' ');
}

static void put_hex(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";

    for (int i = 7; i >= 0; i--)
        put_c(hex[(v >> (i * 4)) & 0xFu]);
}

static void put_dec(uint32_t v)
{
    char buf[10];
    int n = 0;

    do {
        buf[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        put_c(buf[--n]);
}

static void put_label(const char *label)
{
    screen_set_color(RGB565_CYAN);
    put_s(label);
    screen_set_color(RGB565_WHITE);
}

static void line(uint32_t row, const char *label)
{
    screen_set_row(row);
    col = 0;
    screen_set_color(RGB565_CYAN);
    put_s(label);
    pad_to(LABEL_COLS);
    screen_set_color(RGB565_WHITE);
}

static void put_arg(const struct ioctl_entry *e, unsigned i)
{
    if (e->arg_state[i] == ARG_OK)
        put_hex(e->arg[i]);
    else
        put_c(e->arg_state[i] == ARG_NONE ? '-' : '?');
}

/* SYSINTR values since boot. The values since the last draw are white, the
 * others gray. A `+` shows that more values do not fit. */
static void put_sysintrs(void)
{
    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t bit = 1u << (i & 31u);
        uint32_t seen = i < 32u ? seen_lo : seen_hi;
        uint32_t win = i < 32u ? win_lo : win_hi;

        if (!(seen & bit))
            continue;
        if (col + 3u > PANEL_COLS) {
            screen_set_color(RGB565_WHITE);
            put_c('+');
            return;
        }
        screen_set_color(win & bit ? RGB565_WHITE : RGB565_GRAY);
        put_dec(i);
        put_c(' ');
    }
}

static void draw(void)
{
    if (!screen_ready) {
        screen_init(FB, FB_WIDTH, FB_WIDTH - PANEL_WIDTH, 0, PANEL_WIDTH,
                    PANEL_HEIGHT, 1, RGB565_BLACK, true);
        screen_ready = 1;
    }
    line(0, "irq");
    put_dec(irqs);
    put_s(" ");
    put_label("pc");
    put_s(" ");
    put_hex(last_pc);
    line(1, "sysintr");
    put_sysintrs();
    line(2, "ioctl");
    put_dec(ioctls);
    put_s(" ");
    put_label("ch374");
    put_s(" ");
    put_dec(ch374_ioctls);
    screen_set_row(3);
    col = 0;
    screen_set_color(RGB565_CYAN);
    put_s("slot");
    pad_to(5);
    put_s("code");
    pad_to(14);
    put_s("in0");
    pad_to(23);
    put_s("in1");
    screen_set_color(RGB565_WHITE);
    for (unsigned i = 0; i < IOCTL_LOG; i++) {
        const struct ioctl_entry *e = &ioctl_log[i];

        screen_set_row(4 + i);
        col = 0;
        if (!e->code)
            continue;
        if (e->slot == 0xFFu)
            put_c('?');
        else
            put_dec(e->slot);
        pad_to(5);
        put_hex(e->code);
        pad_to(14);
        put_arg(e, 0);
        pad_to(23);
        put_arg(e, 1);
    }
    win_lo = win_hi = 0;
}

void resident_irq(uint32_t pc)
{
    irqs++;
    last_pc = pc;
}

uint32_t resident_sysintr(uint32_t sysintr)
{
    if (sysintr < 32) {
        seen_lo |= 1u << sysintr;
        win_lo |= 1u << sysintr;
    } else if (sysintr < 64) {
        seen_hi |= 1u << (sysintr - 32);
        win_hi |= 1u << (sysintr - 32);
    }
    if (irqs % DRAW_EVERY == 0)
        draw();
    return sysintr;
}

void resident_ioctl(uint32_t code, const uint32_t *in, uint32_t in_size)
{
    const volatile uint8_t *proc = CUR_PROC;
    int ok = readable(in);
    struct ioctl_entry *e;

    ioctls++;
    if (ok && in_size >= 4u && in[0] == CH374_CS_PIN &&
        (code == 0x010120D0u || code == 0x010120D4u || code == 0x010120E4u)) {
        ch374_ioctls++;
        return;
    }
    for (unsigned i = IOCTL_LOG - 1; i > 0; i--)
        ioctl_log[i] = ioctl_log[i - 1];
    e = &ioctl_log[0];
    e->code = code;
    for (unsigned i = 0; i < 2; i++) {
        e->arg[i] = 0;
        if (in_size < 4u * (i + 1))
            e->arg_state[i] = ARG_NONE;
        else if (!ok)
            e->arg_state[i] = ARG_UNREADABLE;
        else {
            e->arg_state[i] = ARG_OK;
            e->arg[i] = in[i];
        }
    }
    e->slot = readable((const void *)proc) ? *proc : 0xFFu;
}
