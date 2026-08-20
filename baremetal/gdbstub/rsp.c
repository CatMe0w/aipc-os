#include "rsp.h"
#include "musb.h"
#include "trace.h"
#include "sig.h"

/* GDB sizes its requests to fill this exactly. The reply buffer must match. */
#define PACKET_SIZE 0x2000u

#define CMD_SIZE    PACKET_SIZE
#define OUT_SIZE    (PACKET_SIZE + 16u)     /* body plus "+$" and "#xx" */

_Static_assert(CMD_SIZE >= PACKET_SIZE, "command buffer smaller than advertised");

/* Words per `monitor md32`, bounded by the reply buffer. */
#define MD32_MAX 0x100u

/* Console output is hex encoded, and the 'O' takes one byte of the body. */
#define MONITOR_CHUNK ((PACKET_SIZE - 8u) / 2u)

#define TRACE_CMD_LIMIT 60u

/* In .data, because start.S fills it before the .bss clear. */
struct arm_regs g_regs __attribute__((section(".data.regs")));

static uint8_t g_cmd[CMD_SIZE] __attribute__((aligned(4)));
static uint32_t g_cmd_len;
static uint8_t g_out[OUT_SIZE] __attribute__((aligned(4)));
static uint32_t g_out_len;
static uint32_t g_body_start;

static uint32_t g_state;
static uint32_t g_sum;
static uint8_t g_csum_hi;
static uint32_t g_traced;
static uint32_t g_last_sig = SIG_TRAP;

/* GDB's own ARM breakpoint encoding. */
#define BP_INSN 0xE7FFDEFEu
#define BP_MAX  16u

static struct {
    uint32_t addr;
    uint32_t saved;
    uint32_t active;
} g_bp[BP_MAX];

static uint32_t *bp_lookup(uint32_t addr);

/* The breakpoint that lands a single step. It goes away as soon as it fires. */
static uint32_t g_step_addr;
static uint32_t g_step_saved;
static uint32_t g_step_active;

enum { ST_IDLE, ST_BODY, ST_CSUM1, ST_CSUM2 };

static const char target_xml[] =
    "<?xml version=\"1.0\"?>"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
    "<target version=\"1.0\">"
    "<architecture>arm</architecture>"
    "<feature name=\"org.gnu.gdb.arm.core\">"
    "<reg name=\"r0\" bitsize=\"32\"/>"
    "<reg name=\"r1\" bitsize=\"32\"/>"
    "<reg name=\"r2\" bitsize=\"32\"/>"
    "<reg name=\"r3\" bitsize=\"32\"/>"
    "<reg name=\"r4\" bitsize=\"32\"/>"
    "<reg name=\"r5\" bitsize=\"32\"/>"
    "<reg name=\"r6\" bitsize=\"32\"/>"
    "<reg name=\"r7\" bitsize=\"32\"/>"
    "<reg name=\"r8\" bitsize=\"32\"/>"
    "<reg name=\"r9\" bitsize=\"32\"/>"
    "<reg name=\"r10\" bitsize=\"32\"/>"
    "<reg name=\"r11\" bitsize=\"32\"/>"
    "<reg name=\"r12\" bitsize=\"32\"/>"
    "<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
    "<reg name=\"lr\" bitsize=\"32\"/>"
    "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
    "<reg name=\"cpsr\" bitsize=\"32\" regnum=\"25\"/>"
    "</feature>"
    "</target>";

static uint32_t hexval(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0xFFFFFFFFu;
}

static char hexdigit(uint32_t v)
{
    return (char)((v < 10) ? ('0' + v) : ('a' + v - 10));
}

static uint32_t parse_hex(const uint8_t **pp, const uint8_t *end)
{
    uint32_t v = 0;

    while (*pp < end) {
        uint32_t d = hexval(**pp);

        if (d == 0xFFFFFFFFu)
            break;
        v = (v << 4) | d;
        (*pp)++;
    }
    return v;
}

static void out_ch(char c)
{
    if (g_out_len < OUT_SIZE)
        g_out[g_out_len++] = (uint8_t)c;
}

static void out_str(const char *s)
{
    while (*s)
        out_ch(*s++);
}

static void out_byte(uint8_t b)
{
    out_ch(hexdigit(b >> 4));
    out_ch(hexdigit(b & 0xF));
}

/* Hex without leading zeros. */
static void out_hex(uint32_t v)
{
    uint32_t shift = 28;

    while (shift && !((v >> shift) & 0xFu))
        shift -= 4;
    for (;;) {
        out_ch(hexdigit((v >> shift) & 0xFu));
        if (!shift)
            break;
        shift -= 4;
    }
}

/* Two bytes as four hex digits in one word store. */
static void out_pair(uint8_t b0, uint8_t b1)
{
    uint32_t w = (uint32_t)(uint8_t)hexdigit(b0 >> 4)
               | ((uint32_t)(uint8_t)hexdigit(b0 & 0xF) << 8)
               | ((uint32_t)(uint8_t)hexdigit(b1 >> 4) << 16)
               | ((uint32_t)(uint8_t)hexdigit(b1 & 0xF) << 24);

    *(u32_alias *)&g_out[g_out_len] = w;
    g_out_len += 4;
}

/* Little-endian, per GDB's register/memory encoding. */
static void out_word(uint32_t v)
{
    uint32_t i;

    for (i = 0; i < 4; i++)
        out_byte((uint8_t)(v >> (8 * i)));
}

/* '+' acknowledges the command. In a multi-packet reply, only the first packet
 * carries one. */
static void out_begin_ack(uint32_t ack)
{
    g_out_len = 0;
    if (ack)
        out_ch('+');
    out_ch('$');
    g_body_start = g_out_len;
}

static void out_begin(void)
{
    out_begin_ack(1);
}

static void out_finish(void)
{
    uint32_t sum = 0;
    uint32_t i;

    for (i = g_body_start; i < g_out_len; i++)
        sum += g_out[i];

    out_ch('#');
    out_byte((uint8_t)sum);
    musb_bulk_send(g_out, g_out_len);
}

static void handle_query(const uint8_t *p, const uint8_t *end)
{
    static const char sup[] = "qSupported";
    static const char xfer[] = "qXfer:features:read:target.xml:";
    uint32_t i;

    for (i = 0; sup[i]; i++)
        if (p + i >= end || p[i] != (uint8_t)sup[i])
            break;
    if (!sup[i]) {
        out_str("PacketSize=");
        out_hex(PACKET_SIZE);
        out_str(";qXfer:features:read+;vContSupported+");
        return;
    }

    for (i = 0; xfer[i]; i++)
        if (p + i >= end || p[i] != (uint8_t)xfer[i])
            break;
    if (!xfer[i]) {
        const uint8_t *q = p + i;
        uint32_t off = parse_hex(&q, end);
        uint32_t len;
        uint32_t total = sizeof(target_xml) - 1;
        uint32_t n;

        if (q < end && *q == ',')
            q++;
        len = parse_hex(&q, end);

        if (off >= total) {
            out_str("l");
            return;
        }
        n = total - off;
        if (n > len)
            n = len;
        if (n > PACKET_SIZE - 16)
            n = PACKET_SIZE - 16;

        out_ch((off + n < total) ? 'm' : 'l');
        for (i = 0; i < n; i++)
            out_ch(target_xml[off + i]);
        return;
    }

    if (p + 8 <= end && p[1] == 'A' && p[2] == 't' && p[3] == 't') {
        out_str("1");                       /* qAttached */
        return;
    }
    if (end - p == 2 && p[1] == 'C') {      /* exactly qC, not qCRC */
        out_str("QC1");
        return;
    }
    if (p + 12 <= end && p[1] == 'f' && p[2] == 'T') {
        out_str("m1");                      /* qfThreadInfo */
        return;
    }
    if (p + 12 <= end && p[1] == 's' && p[2] == 'T') {
        out_str("l");                       /* qsThreadInfo */
        return;
    }
    /* Anything else: empty reply, which RSP reads as unsupported. */
}

/* Length of s when p starts with it, 0 otherwise. */
static uint32_t prefix(const uint8_t *p, const uint8_t *end, const char *s)
{
    uint32_t i;

    for (i = 0; s[i]; i++)
        if (p + i >= end || p[i] != (uint8_t)s[i])
            return 0;
    return i;
}

/* Console output packet. GDB prints these as they arrive. */
static void send_output(const uint8_t *text, uint32_t len, uint32_t ack)
{
    uint32_t i;

    out_begin_ack(ack);
    out_ch('O');
    for (i = 0; i < len; i++)
        out_byte(text[i]);
    out_finish();
}

/* Hex encoded, as GDB expects for monitor output. */
static void send_text(const char *s, uint32_t ack)
{
    out_begin_ack(ack);
    while (*s)
        out_byte((uint8_t)*s++);
    out_finish();
}

/* DDR words the ROM vectors forward to. See docs/bootrom/memory-map.md. */
#define VEC_UNDEF   0x30000004u
#define VEC_PABORT  0x3000000Cu
#define VEC_DABORT  0x30000010u
#define VEC_LITERAL 0x30000800u

void bp_install(void)
{
    volatile uint32_t *lit = (volatile uint32_t *)(uintptr_t)VEC_LITERAL;

    lit[0] = (uint32_t)(uintptr_t)&bp_undef;
    lit[1] = (uint32_t)(uintptr_t)&bp_pabort;
    lit[2] = (uint32_t)(uintptr_t)&bp_dabort;

    REG32(VEC_UNDEF)  = 0xE59FF7F4u;    /* ldr pc, [pc, #0x7f4] -> lit[0] */
    REG32(VEC_PABORT) = 0xE59FF7F0u;    /* ldr pc, [pc, #0x7f0] -> lit[1] */
    REG32(VEC_DABORT) = 0xE59FF7F0u;    /* ldr pc, [pc, #0x7f0] -> lit[2] */
}

/* Entered from bp.S. Does not return. */
void rsp_trapped(uint32_t sig)
{
    if (g_step_active) {
        REG32(g_step_addr) = g_step_saved;
        g_step_active = 0;
        if (g_regs.pc == g_step_addr)
            sig = SIG_TRAP;
    }
    if (bp_lookup(g_regs.pc))
        sig = SIG_TRAP;

    g_last_sig = sig;

    trace_puts("trap sig=");
    trace_dec(sig);
    trace_puts(" pc=0x");
    trace_hex(g_regs.pc, 8);
    trace_puts(" cpsr=0x");
    trace_hex(g_regs.cpsr, 8);
    trace_puts("\n");

    /* Unsolicited stop reply. */
    out_begin_ack(0);
    out_ch('S');
    out_byte((uint8_t)sig);
    out_finish();

    for (;;)
        musb_poll();
}

static uint32_t equals(const uint8_t *p, uint32_t len, const char *s)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        if (!s[i] || p[i] != (uint8_t)s[i])
            return 0;
    return s[len] == 0;
}

/* Long text as a run of console output packets, closed with OK. */
static void send_region(uint32_t base, uint32_t len)
{
    uint32_t off = 0;
    uint32_t ack = 1;

    while (off < len) {
        uint32_t chunk = len - off;

        if (chunk > MONITOR_CHUNK)
            chunk = MONITOR_CHUNK;
        send_output((const uint8_t *)(uintptr_t)(base + off), chunk, ack);
        ack = 0;
        off += chunk;
    }

    out_begin_ack(ack);
    out_str("OK");
    out_finish();
}

static uint32_t parse_dec(const uint8_t **pp, const uint8_t *end)
{
    uint32_t v = 0;

    while (*pp < end && **pp >= '0' && **pp <= '9') {
        v = v * 10u + (uint32_t)(**pp - '0');
        (*pp)++;
    }
    return v;
}

/* Monitor replies are hex encoded. */
static void mon_ch(char c)
{
    out_byte((uint8_t)c);
}

static void mon_str(const char *s)
{
    while (*s)
        mon_ch(*s++);
}

static void mon_word(uint32_t v)
{
    uint32_t k;

    mon_str("0x");
    for (k = 8; k > 0; k--)
        mon_ch(hexdigit((v >> ((k - 1) * 4)) & 0xFu));
}

/* Repeated subtraction, because there is no libgcc here to divide with. */
static void mon_dec(uint32_t v)
{
    static const uint32_t pow10[] = { 1000000000u, 100000000u, 10000000u,
                                      1000000u, 100000u, 10000u, 1000u,
                                      100u, 10u, 1u };
    uint32_t i;
    uint32_t started = 0;

    for (i = 0; i < 10; i++) {
        uint32_t n = 0;

        while (v >= pow10[i]) {
            v -= pow10[i];
            n++;
        }
        if (n || started || i == 9) {
            mon_ch((char)('0' + n));
            started = 1;
        }
    }
}

/* `monitor md32 0x<addr> [<words>]`. The word-wide counterpart of `m`. */
static void monitor_md32(const uint8_t *p, const uint8_t *end)
{
    uint32_t addr;
    uint32_t count;
    uint32_t i;

    while (p < end && *p == ' ')
        p++;
    if (p + 2 > end || p[0] != '0' || p[1] != 'x') {
        send_text("md32: address must be hex with an 0x prefix\n", 1);
        return;
    }
    p += 2;
    addr = parse_hex(&p, end) & ~3u;

    while (p < end && *p == ' ')
        p++;
    count = parse_dec(&p, end);
    if (!count)
        count = 4;

    out_begin_ack(1);
    if (count > MD32_MAX) {
        mon_str("clamped to ");
        mon_dec(MD32_MAX);
        mon_str(" words\n");
        count = MD32_MAX;
    }
    for (i = 0; i < count; i++) {
        if (!(i & 3u)) {
            mon_word(addr + i * 4);
            mon_ch(':');
        }
        mon_ch(' ');
        mon_word(REG32(addr + i * 4));
        if ((i & 3u) == 3u || i + 1 == count)
            mon_ch('\n');
    }
    out_finish();
}

/* qRcmd. This emits its own packets, so it runs before dispatch() opens one. */
static void handle_monitor(const uint8_t *p, const uint8_t *end)
{
    uint8_t cmd[64];
    uint32_t n = 0;

    while (p + 1 < end && n < sizeof(cmd) - 1) {
        cmd[n++] = (uint8_t)((hexval(p[0]) << 4) | hexval(p[1]));
        p += 2;
    }

    if (n > 5 && equals(cmd, 5, "md32 "))
        monitor_md32(cmd + 5, cmd + n);
    else if (equals(cmd, n, "trace"))
        send_region(TRACE_BASE, trace_used());
    else if (equals(cmd, n, "oldtrace"))
        send_region(TRACE_PREV, trace_prev_used());
    else
        send_text("commands: trace, oldtrace, md32 0x<addr> [<words>]\n", 1);
}

/* First action of a vCont. There is one thread, so that action applies. */
static uint8_t vcont_action(const uint8_t *p, const uint8_t *end)
{
    while (p < end && *p != ';')
        p++;
    if (p < end)
        p++;
    return (p < end) ? *p : 0;
}

static uint32_t *bp_lookup(uint32_t addr)
{
    uint32_t i;

    for (i = 0; i < BP_MAX; i++)
        if (g_bp[i].active && g_bp[i].addr == addr)
            return &g_bp[i].saved;
    return 0;
}

static uint32_t bp_set(uint32_t addr)
{
    uint32_t i;

    if (bp_lookup(addr))
        return 1;
    for (i = 0; i < BP_MAX; i++) {
        if (g_bp[i].active)
            continue;
        g_bp[i].addr = addr;
        g_bp[i].saved = REG32(addr);
        g_bp[i].active = 1;
        REG32(addr) = BP_INSN;
        return 1;
    }
    return 0;
}

static uint32_t bp_clear(uint32_t addr)
{
    uint32_t i;

    for (i = 0; i < BP_MAX; i++) {
        if (!g_bp[i].active || g_bp[i].addr != addr)
            continue;
        REG32(addr) = g_bp[i].saved;
        g_bp[i].active = 0;
        return 1;
    }
    return 1;               /* removing an unset one is not an error */
}

/* Software breakpoints only. The hardware ones need the JTAG scan chain. */
static void handle_breakpoint(const uint8_t *p, const uint8_t *end)
{
    uint32_t set = (*p == 'Z');
    uint32_t addr;

    p++;
    if (p >= end || *p != '0')
        return;             /* empty reply: unsupported type */
    p++;
    if (p < end && *p == ',')
        p++;
    addr = parse_hex(&p, end);

    out_str((set ? bp_set(addr) : bp_clear(addr)) ? "OK" : "E01");
}

/* Byte-wide reads, so that a hardware register sees byte-lane accesses. */
static void read_memory(const uint8_t *p, const uint8_t *end)
{
    uint32_t addr = parse_hex(&p, end);
    uint32_t len;
    uint32_t i = 0;

    if (p < end && *p == ',')
        p++;
    len = parse_hex(&p, end);

    if (len > (OUT_SIZE - 16) / 2)
        len = (OUT_SIZE - 16) / 2;

    /* Align g_out_len to a word boundary for out_pair(). */
    while (i < len && (g_out_len & 3u))
        out_byte(*(volatile uint8_t *)(uintptr_t)(addr + i++));

    while (i + 2 <= len) {
        uint8_t b0 = *(volatile uint8_t *)(uintptr_t)(addr + i);
        uint8_t b1 = *(volatile uint8_t *)(uintptr_t)(addr + i + 1);

        out_pair(b0, b1);
        i += 2;
    }

    while (i < len)
        out_byte(*(volatile uint8_t *)(uintptr_t)(addr + i++));
}

static void write_memory_hex(const uint8_t *p, const uint8_t *end)
{
    uint32_t addr = parse_hex(&p, end);
    uint32_t len;
    uint32_t i;

    if (p < end && *p == ',')
        p++;
    len = parse_hex(&p, end);
    if (p < end && *p == ':')
        p++;

    for (i = 0; i < len && p + 1 < end; i++, p += 2)
        *(volatile uint8_t *)(uintptr_t)(addr + i) =
            (uint8_t)((hexval(p[0]) << 4) | hexval(p[1]));

    out_str("OK");
}

/* Binary write (`X`). Word stores when the address is aligned and the run has
 * no escapes. The MMU is off, so each byte store is its own bus transaction. */
static void write_memory_bin(const uint8_t *p, const uint8_t *end)
{
    uint32_t addr = parse_hex(&p, end);
    uint32_t len;
    uint32_t i = 0;

    if (p < end && *p == ',')
        p++;
    len = parse_hex(&p, end);
    if (p < end && *p == ':')
        p++;

    while (i < len && p < end) {
        if (!((addr + i) & 3u) && i + 4 <= len && p + 4 <= end &&
            p[0] != 0x7D && p[1] != 0x7D && p[2] != 0x7D && p[3] != 0x7D) {
            *(volatile uint32_t *)(uintptr_t)(addr + i) =
                (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            p += 4;
            i += 4;
        } else {
            uint8_t b = *p++;

            if (b == 0x7D && p < end)
                b = (uint8_t)(*p++ ^ 0x20);
            *(volatile uint8_t *)(uintptr_t)(addr + i) = b;
            i++;
        }
    }

    out_str("OK");
}

static void read_regs(void)
{
    uint32_t i;

    for (i = 0; i < 13; i++)
        out_word(g_regs.r[i]);
    out_word(g_regs.sp);
    out_word(g_regs.lr);
    out_word(g_regs.pc);
    out_word(g_regs.cpsr);
}

static void write_regs(const uint8_t *p, const uint8_t *end)
{
    uint32_t *slot = (uint32_t *)&g_regs;
    uint32_t i, j;

    for (i = 0; i < sizeof(g_regs) / 4; i++) {
        uint32_t v = 0;

        for (j = 0; j < 4; j++) {
            if (p + 1 >= end)
                break;
            v |= ((hexval(p[0]) << 4) | hexval(p[1])) << (8 * j);
            p += 2;
        }
        slot[i] = v;
    }

    out_str("OK");
}

static void rsp_resume_here(void)
{
    g_out_len = 0;
    out_ch('+');
    musb_bulk_send(g_out, g_out_len);
    rsp_resume(&g_regs);
}

static void resume(const uint8_t *p, const uint8_t *end)
{
    if (p < end)
        g_regs.pc = parse_hex(&p, end);

    g_out_len = 0;
    out_ch('+');
    musb_bulk_send(g_out, g_out_len);

    trace_puts("resume pc=0x");
    trace_hex(g_regs.pc, 8);
    trace_puts("\n");

    rsp_resume(&g_regs);
}

/* Plant a breakpoint at the next pc and continue. The core has no step bit. */
static void do_step(void)
{
    uint32_t next;

    if (!arm_next_pc(&next)) {
        out_begin();
        out_str("E01");
        out_finish();
        return;
    }

    /* A branch to self has nowhere to stop, so report the trap now. */
    if (next == g_regs.pc) {
        out_begin();
        out_ch('S');
        out_byte((uint8_t)SIG_TRAP);
        out_finish();
        return;
    }

    if (!bp_lookup(next)) {
        g_step_addr = next;
        g_step_saved = REG32(next);
        REG32(next) = BP_INSN;
        g_step_active = 1;
    }

    trace_puts("step to 0x");
    trace_hex(next, 8);
    trace_puts("\n");

    rsp_resume_here();
}

static void dispatch(void)
{
    const uint8_t *p = g_cmd;
    const uint8_t *end = g_cmd + g_cmd_len;

    if (g_traced < TRACE_CMD_LIMIT) {
        uint32_t i;

        char echo[25];

        g_traced++;
        for (i = 0; i < g_cmd_len && i < sizeof(echo) - 1; i++)
            echo[i] = (char)g_cmd[i];
        echo[i] = 0;
        trace_puts("cmd ");
        trace_puts(echo);
        trace_puts("\n");
    }

    if (!g_cmd_len) {
        out_begin();
        out_finish();
        return;
    }

    if (g_cmd[0] == 'c' || g_cmd[0] == 'C') {
        resume(p + 1, end);
        return; /* not reached */
    }

    if (g_cmd[0] == 's' && g_cmd_len == 1) {
        do_step();
        return;
    }

    if (prefix(p, end, "vCont;")) {
        uint8_t act = vcont_action(p + 5, end);

        if (act == 'c' || act == 'C') {
            resume(end, end);
            return; /* not reached */
        }
        if (act == 's' || act == 'S') {
            do_step();
            return;
        }
        /* anything else falls through to an empty reply */
    }

    {
        uint32_t n = prefix(p, end, "qRcmd,");

        if (n) {
            handle_monitor(p + n, end);
            return;
        }
    }

    out_begin();

    switch (g_cmd[0]) {
    case '?':
        out_ch('S');
        out_byte((uint8_t)g_last_sig);
        break;
    case 'g':
        read_regs();
        break;
    case 'G':
        write_regs(p + 1, end);
        break;
    case 'm':
        read_memory(p + 1, end);
        break;
    case 'M':
        write_memory_hex(p + 1, end);
        break;
    case 'X':
        write_memory_bin(p + 1, end);
        break;
    case 'q':
        handle_query(p, end);
        break;
    case 'v':
        if (prefix(p, end, "vCont?"))
            out_str("vCont;c;C;s;S");
        break;

    case 'Z':
    case 'z':
        handle_breakpoint(p, end);
        break;

    case 'H':
    case 'D':
    case 'k':
        out_str("OK");
        break;
    default:
        /* Empty reply, which RSP reads as unsupported. */
        break;
    }

    out_finish();
}

/* Framing only. feed_body() copies the body in runs. */
static void feed_byte(uint8_t c)
{
    switch (g_state) {
    case ST_IDLE:
        if (c == '$') {
            g_cmd_len = 0;
            g_sum = 0;
            g_state = ST_BODY;
        }
        /* Ignore ack, nak and break. There is no retransmit and no
         * interrupt target. */
        break;

    case ST_CSUM1:
        g_csum_hi = c;
        g_state = ST_CSUM2;
        break;

    default:
        g_state = ST_IDLE;
        if (((hexval(g_csum_hi) << 4) | hexval(c)) == (g_sum & 0xFF)) {
            dispatch();
        } else {
            g_out_len = 0;
            out_ch('-');
            musb_bulk_send(g_out, g_out_len);
        }
        break;
    }
}

/* Copy the run up to '#' and sum it. Returns the number of bytes consumed. */
static uint32_t feed_body(const uint8_t *data, uint32_t len)
{
    uint32_t sum = g_sum;
    uint32_t n = g_cmd_len;
    uint32_t i = 0;

    while (i < len) {
        uint8_t c = data[i];

        if (c == '#')
            break;
        if (n >= CMD_SIZE) {
            g_state = ST_IDLE;              /* oversized, so drop it */
            break;
        }
        g_cmd[n++] = c;
        sum += c;
        i++;
    }

    g_cmd_len = n;
    g_sum = sum;
    return i;
}

void rsp_feed(const uint8_t *data, uint32_t len)
{
    while (len) {
        uint32_t n;

        if (g_state != ST_BODY) {
            feed_byte(*data++);
            len--;
            continue;
        }

        n = feed_body(data, len);
        data += n;
        len -= n;

        if (g_state == ST_BODY && len) {    /* stopped on '#' */
            g_state = ST_CSUM1;
            data++;
            len--;
        }
    }
}
