#include "lcd_timing.h"
#include "lcd.h"
#include "mmu.h"
#include "trace.h"

#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL(off)  REG32(0x08000000u + (off))
#define LCD(off)      REG32(0x20010000u + (off))

#define RAMCTRL_AHB_PRIORITY  0x2002D014u

#define LCD_RGB_CTRL2     0x14u
#define LCD_VIRT_LEN      0x18u
#define LCD_VIRT_OFFSET   0x1Cu
#define LCD_STATUS        0xBCu
#define LCD_SOFT_CTRL     0xC8u
#define LCD_CLOCK_CONF    0xE8u

#define RGB_VIR_EN        (1u << 28)
#define STATUS_RGB_OK     (1u << 3)
#define STATUS_RGB_START  (1u << 4)
#define SOFT_FAST_DMA     (1u << 17)

#define TIMER1_CTRL       0x18u
#define TIMER1_LIVE       0x100u
#define TIMER_COUNT_MASK  0x03FFFFFFu
#define TIMER_ENABLE      0x04000000u
#define TIMER_HZ          12000000u

#define BOOT_CLOCK_CONF   0x00000111u
#define WINDOW_MS         2000u

/* 0xC8 reads back as zero, so keep a shadow copy instead of read-modify-write. */
#define SOFT_SW_EN        (1u << 11)
static uint32_t g_soft_ctrl = SOFT_SW_EN;

static void soft_ctrl_set(uint32_t value)
{
    g_soft_ctrl = value;
    LCD(LCD_SOFT_CTRL) = value;
}

static volatile uint32_t *g_out;

static void out(uint32_t v)
{
    *g_out++ = v;
}

static uint32_t udiv32(uint32_t num, uint32_t den)
{
    uint32_t quot = 0;
    uint32_t bit = 1;

    if (den == 0 || num < den)
        return 0;

    while (den <= num >> 1) {
        den <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (num >= den) {
            num -= den;
            quot |= bit;
        }
        den >>= 1;
        bit >>= 1;
    }
    return quot;
}

static void tmr_start(void)
{
    SYSCTRL(TIMER1_CTRL) = TIMER_COUNT_MASK;
    SYSCTRL(TIMER1_CTRL) = TIMER_COUNT_MASK | TIMER_ENABLE;
}

static uint32_t tmr_now(void)
{
    return SYSCTRL(TIMER1_LIVE) & TIMER_COUNT_MASK;
}

static uint32_t tmr_since(uint32_t start)
{
    return (start - tmr_now()) & TIMER_COUNT_MASK;
}

static void tmr_delay_ms(uint32_t ms)
{
    uint32_t t0 = tmr_now();
    uint32_t want = (TIMER_HZ / 1000u) * ms;

    while (tmr_since(t0) < want)
        ;
}

static void probe_ahb_priority(void)
{
    uint32_t r0 = REG32(RAMCTRL_AHB_PRIORITY);
    uint32_t r1, r2;

    REG32(RAMCTRL_AHB_PRIORITY) = r0 & ~0x0000007Fu;
    r1 = REG32(RAMCTRL_AHB_PRIORITY);

    REG32(RAMCTRL_AHB_PRIORITY) = 0xFFFFFFFFu;
    r2 = REG32(RAMCTRL_AHB_PRIORITY);

    REG32(RAMCTRL_AHB_PRIORITY) = r0 & ~0x0000007Fu;

    trace_puts("ahb priority 0x2002D014\n");
    trace_reg("  first read", r0);
    trace_reg("  after clear 0x7f", r1);
    trace_reg("  after all ones", r2);
    out(r0);
    out(r1);
    out(r2);
}

static void probe_status_semantics(void)
{
    uint32_t s0 = LCD(LCD_STATUS);
    uint32_t s1 = LCD(LCD_STATUS);
    uint32_t s2, s3;

    LCD(LCD_STATUS) = 0xFFFFFFFFu;
    s2 = LCD(LCD_STATUS);

    LCD(LCD_STATUS) = 0u;
    s3 = LCD(LCD_STATUS);

    trace_puts("status 0x2001,00BC\n");
    trace_reg("  read 1", s0);
    trace_reg("  read 2", s1);
    trace_reg("  after write ones", s2);
    trace_reg("  after write zero", s3);
    out(s0);
    out(s1);
    out(s2);
    out(s3);
}

struct fcount {
    uint32_t ok_edges;
    uint32_t start_edges;
    uint32_t ok_high;
    uint32_t samples;
    uint32_t ticks;
};

static void count_frames(uint32_t ms, uint32_t clear_mask, struct fcount *r)
{
    uint32_t want = (TIMER_HZ / 1000u) * ms;
    uint32_t t0 = tmr_now();
    uint32_t prev = LCD(LCD_STATUS);

    r->ok_edges = 0;
    r->start_edges = 0;
    r->ok_high = 0;
    r->samples = 0;

    for (;;) {
        uint32_t s = LCD(LCD_STATUS);

        r->samples++;
        if (s & STATUS_RGB_OK)
            r->ok_high++;
        if ((s & STATUS_RGB_OK) && !(prev & STATUS_RGB_OK))
            r->ok_edges++;
        if ((s & STATUS_RGB_START) && !(prev & STATUS_RGB_START))
            r->start_edges++;

        if (clear_mask)
            LCD(LCD_STATUS) = clear_mask;

        prev = s;
        if (tmr_since(t0) >= want)
            break;
    }

    r->ticks = tmr_since(t0);
}

static uint32_t hz_x100(uint32_t edges, uint32_t ticks)
{
    if (ticks < 1000u)
        return 0;
    return udiv32(edges * 1200000u, ticks / 1000u);
}

static void report_count(const char *tag, const struct fcount *r)
{
    trace_puts(tag);
    trace_puts(": ok_edges=");
    trace_dec(r->ok_edges);
    trace_puts(" start_edges=");
    trace_dec(r->start_edges);
    trace_puts(" ok_high=");
    trace_dec(r->ok_high);
    trace_puts("/");
    trace_dec(r->samples);
    trace_puts(" ticks=");
    trace_dec(r->ticks);
    trace_puts(" cHz=");
    trace_dec(hz_x100(r->ok_edges, r->ticks));
    trace_puts("\n");

    out(r->ok_edges);
    out(r->start_edges);
    out(r->ok_high);
    out(r->samples);
    out(r->ticks);
}

static const uint8_t g_dividers[] = { 8, 4 };

static void probe_divider_sweep(void)
{
    struct fcount r;

    trace_puts("divider sweep, field in 0x2001,00E8 bits 7:1\n");

    for (uint32_t i = 0; i < sizeof(g_dividers); i++) {
        uint32_t f = g_dividers[i];

        LCD(LCD_CLOCK_CONF) = (1u << 8) | (f << 1) | 1u;
        tmr_delay_ms(150u);

        count_frames(WINDOW_MS, 0u, &r);

        out(f);
        trace_puts("  field=");
        trace_dec(f);
        report_count("", &r);
    }

    LCD(LCD_CLOCK_CONF) = BOOT_CLOCK_CONF;
    tmr_delay_ms(150u);
}

#define BENCH_WORDS  (FB_BYTES / 4u)

static uint32_t bench_write(void)
{
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)FB_ADDR;
    uint32_t t0 = tmr_now();

    for (uint32_t i = 0; i < BENCH_WORDS; i++)
        fb[i] = 0x12345678u;

    return tmr_since(t0);
}

static volatile uint32_t g_sink;

static uint32_t bench_read(void)
{
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)FB_ADDR;
    uint32_t acc = 0;
    uint32_t t0 = tmr_now();

    for (uint32_t i = 0; i < BENCH_WORDS; i++)
        acc += fb[i];

    g_sink = acc;
    return tmr_since(t0);
}

static uint32_t kib_per_s(uint32_t ticks)
{
    if (ticks < 1000u)
        return 0;
    return udiv32((FB_BYTES / 1024u) * 12000u, ticks / 1000u);
}

static void probe_soft_ctrl_readback(void)
{
    trace_reg("soft ctrl 0xC8 read back", LCD(LCD_SOFT_CTRL));
    out(LCD(LCD_SOFT_CTRL));
}

#define DDR_MAP_BASE     0x30000000u
#define DDR_MAP_SIZE     0x04000000u
#define SYSCTRL_MAP      0x08000000u
#define PERIPH_MAP       0x20000000u
#define L2_MAP           0x48000000u

#define TRAFFIC_BASE     0x31000000u
#define TRAFFIC_BYTES    0x00400000u
#define TRAFFIC_WORDS    (TRAFFIC_BYTES / 4u)

#define LINE_WORDS       8u
#define LINES_PER_SAMPLE 2048u

#define BLIT_WORDS       2048u

#define STATUS_BUF_EMPTY (1u << 18)
#define STATUS_SYS_ERROR (1u << 0)

#define LCD_INT_ENABLE   0xC0u
#define LCD_OPERATE      0xB8u
#define OPERATE_SYS_STOP (1u << 0)
#define OPERATE_RGB_GO   (1u << 2)

struct phase2 {
    uint32_t dev_write, dev_read;
    uint32_t buf_write, buf_read;
    uint32_t curve_ticks[2][4];
    uint32_t curve_status[2][4];
    uint32_t margin_ticks[3], margin_status[3], margin_alive[3];
    uint32_t rec_before[5], rec_after[5];
};

static struct phase2 g_p2;

static void map_common(uint32_t fb_flags)
{
    mmu_reset();
    mmu_map(DDR_MAP_BASE, DDR_MAP_SIZE, MMU_CACHED);
    mmu_map(FB_ADDR, 3u * MMU_SECTION_SIZE, fb_flags);
    mmu_map(SYSCTRL_MAP, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(PERIPH_MAP, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_map(L2_MAP, MMU_SECTION_SIZE, MMU_DEVICE);
    mmu_start();
}

static uint32_t timed_work(uint32_t prio_low, uint32_t iters, uint32_t *status_or)
{
    volatile uint32_t *buf = (volatile uint32_t *)(uintptr_t)TRAFFIC_BASE;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)FB_ADDR;
    uint32_t idx = 0, fb_idx = 0, acc = 0, t0;

    REG32(RAMCTRL_AHB_PRIORITY) = 0xFFFFFF80u | prio_low;
    drain_write_buffer();
    (void)LCD(LCD_STATUS);

    t0 = tmr_now();
    for (uint32_t k = 0; k < iters; k++) {
        for (uint32_t n = 0; n < LINES_PER_SAMPLE; n++) {
            buf[idx] = buf[idx] + 1u;
            idx += LINE_WORDS;
            if (idx >= TRAFFIC_WORDS)
                idx = 0;
        }
        for (uint32_t n = 0; n < BLIT_WORDS; n++) {
            fb[fb_idx] = 0x07E007E0u;
            if (++fb_idx >= FB_BYTES / 4u)
                fb_idx = 0;
        }
        acc |= LCD(LCD_STATUS);
    }
    *status_or = acc;
    return tmr_since(t0);
}

#define CURVE_ITERS  256u

static void set_field(uint32_t f)
{
    LCD(LCD_CLOCK_CONF) = (1u << 8) | (f << 1) | 1u;
    tmr_delay_ms(200u);
}

static uint32_t alive_edges(void)
{
    struct fcount fc;

    count_frames(200u, 0u, &fc);
    return fc.ok_edges;
}

static void probe_margin(void)
{
    static const uint8_t field[3] = { 4u, 3u, 2u };

    for (uint32_t i = 0; i < 3u; i++) {
        uint32_t st;

        lcd_init();
        set_field(field[i]);
        g_p2.margin_ticks[i] = timed_work(0x00u, CURVE_ITERS, &st);
        g_p2.margin_status[i] = st;
        g_p2.margin_alive[i] = alive_edges();
    }
}

static void force_crash(void)
{
    uint32_t st;

    set_field(2u);
    (void)timed_work(0x7Fu, CURVE_ITERS, &st);
    REG32(RAMCTRL_AHB_PRIORITY) = 0xFFFFFF80u;
    drain_write_buffer();
}

static void probe_recovery(void)
{
    for (uint32_t step = 0; step < 5u; step++) {
        force_crash();
        g_p2.rec_before[step] = alive_edges();

        switch (step) {
        case 0:
            break;
        case 1:
            soft_ctrl_set(SOFT_SW_EN);
            break;
        case 2:
            LCD(LCD_OPERATE) = OPERATE_SYS_STOP;
            tmr_delay_ms(50u);
            LCD(LCD_OPERATE) = OPERATE_RGB_GO;
            break;
        case 3:
            LCD(LCD_RGB_CTRL2) = FB_DMA_ADDR;
            soft_ctrl_set(SOFT_SW_EN);
            break;
        default:
            lcd_init();
            break;
        }

        tmr_delay_ms(100u);
        g_p2.rec_after[step] = alive_edges();

        if (step != 4u)
            lcd_init();
    }
}

static void probe_priority_curve(void)
{
    static const uint8_t field[2] = { 8u, 2u };
    static const uint8_t prio[4] = { 0x00u, 0x7Fu, 0x7Fu, 0x00u };

    for (uint32_t f = 0; f < 2u; f++) {
        LCD(LCD_CLOCK_CONF) = (1u << 8) | ((uint32_t)field[f] << 1) | 1u;
        tmr_delay_ms(200u);

        for (uint32_t i = 0; i < 4u; i++) {
            uint32_t st;
            uint32_t ticks = timed_work(prio[i], CURVE_ITERS, &st);

            g_p2.curve_ticks[f][i] = ticks;
            g_p2.curve_status[f][i] = st;
        }
    }

    LCD(LCD_CLOCK_CONF) = BOOT_CLOCK_CONF;
    tmr_delay_ms(200u);
}

/* The MMU must be off on return: the GDB stub then accesses MUSB at 0x70000000,
 * which this map omits. */
static void probe_phase2(void)
{
    map_common(MMU_DEVICE);
    g_p2.dev_write = bench_write();
    g_p2.dev_read = bench_read();

    LCD(LCD_INT_ENABLE) = LCD(LCD_INT_ENABLE) | STATUS_BUF_EMPTY | STATUS_SYS_ERROR;

    probe_priority_curve();
    lcd_init();
    probe_margin();
    lcd_init();
    probe_recovery();
    set_field(8u);
    REG32(RAMCTRL_AHB_PRIORITY) = 0xFFFFFF80u;
    drain_write_buffer();
    mmu_cache_disable();

    map_common(MMU_BUFFERED);
    g_p2.buf_write = bench_write();
    g_p2.buf_read = bench_read();
    mmu_cache_disable();

    trace_puts("phase 2, MMU on\n");
    trace_puts("  fb device   write ticks=");
    trace_dec(g_p2.dev_write);
    trace_puts(" KiB/s=");
    trace_dec(kib_per_s(g_p2.dev_write));
    trace_puts(" read KiB/s=");
    trace_dec(kib_per_s(g_p2.dev_read));
    trace_puts("\n");
    trace_puts("  fb buffered write ticks=");
    trace_dec(g_p2.buf_write);
    trace_puts(" KiB/s=");
    trace_dec(kib_per_s(g_p2.buf_write));
    trace_puts(" read KiB/s=");
    trace_dec(kib_per_s(g_p2.buf_read));
    trace_puts("\n");

    out(g_p2.dev_write);
    out(g_p2.dev_read);
    out(g_p2.buf_write);
    out(g_p2.buf_read);

    for (uint32_t f = 0; f < 2u; f++) {
        static const uint8_t field[2] = { 8u, 2u };
        static const uint8_t prio[4] = { 0x00u, 0x7Fu, 0x7Fu, 0x00u };

        trace_puts("  priority curve, field=");
        trace_dec(field[f]);
        trace_puts("\n");
        for (uint32_t i = 0; i < 4u; i++) {
            trace_puts("    low7=");
            trace_hex(prio[i], 2);
            trace_puts(" ticks=");
            trace_dec(g_p2.curve_ticks[f][i]);
            trace_puts(" status_or=");
            trace_hex(g_p2.curve_status[f][i], 8);
            trace_puts("\n");
            out(g_p2.curve_ticks[f][i]);
            out(g_p2.curve_status[f][i]);
        }
    }

    {
        static const uint8_t mf[3] = { 4u, 3u, 2u };

        trace_puts("  margin, low7=00 under load\n");
        for (uint32_t i = 0; i < 3u; i++) {
            trace_puts("    field=");
            trace_dec(mf[i]);
            trace_puts(" ticks=");
            trace_dec(g_p2.margin_ticks[i]);
            trace_puts(" status_or=");
            trace_hex(g_p2.margin_status[i], 8);
            trace_puts(" edges_after=");
            trace_dec(g_p2.margin_alive[i]);
            trace_puts("\n");
            out(g_p2.margin_ticks[i]);
            out(g_p2.margin_status[i]);
            out(g_p2.margin_alive[i]);
        }

        trace_puts("  recovery, edges over 200 ms, 0 means dead\n");
        for (uint32_t s = 0; s < 5u; s++) {
            trace_puts("    step=");
            trace_dec(s);
            trace_puts(" before=");
            trace_dec(g_p2.rec_before[s]);
            trace_puts(" after=");
            trace_dec(g_p2.rec_after[s]);
            trace_puts("\n");
            out(g_p2.rec_before[s]);
            out(g_p2.rec_after[s]);
        }
        trace_puts("    0 none, 1 sw_en, 2 stop+go, 3 base+sw_en, 4 lcd_init\n");
    }

    LCD(LCD_OPERATE) = OPERATE_SYS_STOP;
    tmr_delay_ms(50u);
    LCD(LCD_OPERATE) = OPERATE_RGB_GO;
    tmr_delay_ms(50u);
    trace_puts("  scanout restarted, check the panel for a shift\n");
}

static void setup_virtual_page(void)
{
    static const uint16_t band[8] = {
        0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u,
        0x07FFu, 0xF81Fu, 0xFFFFu, 0x8410u,
    };
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)FB_ADDR;
    uint32_t row_in_band = 0;
    uint32_t band_index = 0;

    for (uint32_t y = 0; y < PAINT_ROWS; y++) {
        uint16_t c = (row_in_band == 0u) ? 0x0000u : band[band_index & 7u];

        for (uint32_t x = 0; x < FB_WIDTH; x++)
            *fb++ = c;

        if (++row_in_band == 48u) {
            row_in_band = 0;
            band_index++;
        }
    }

    LCD(LCD_VIRT_LEN) = (FB_WIDTH << 16) | PAINT_ROWS;
    LCD(LCD_VIRT_OFFSET) = 0u;
    LCD(LCD_RGB_CTRL2) = FB_DMA_ADDR;
    soft_ctrl_set(SOFT_SW_EN);

    trace_puts("page painted 800x");
    trace_dec(PAINT_ROWS);
    trace_puts(", black line every 48 rows\n");
    trace_puts("pan: set 0x20010014 = 0x03b00000 + rows*1600, then 0x200100C8 = 0x800\n");
}

void lcd_timing_run(void)
{
    g_out = (volatile uint32_t *)(uintptr_t)LCD_RESULT_BASE;

    tmr_start();
    lcd_init();
    tmr_delay_ms(200u);

    probe_ahb_priority();
    probe_status_semantics();

    {
        struct fcount r;

        count_frames(WINDOW_MS, 0u, &r);
        report_count("boot divider, no clear", &r);

        count_frames(WINDOW_MS, STATUS_RGB_OK | STATUS_RGB_START, &r);
        report_count("boot divider, w1c", &r);
    }

    probe_divider_sweep();
    probe_soft_ctrl_readback();
    probe_phase2();
    setup_virtual_page();

    trace_puts("lcd-timing done\n");
}
