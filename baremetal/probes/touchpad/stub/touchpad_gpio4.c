#include <stdint.h>

#define R(o) (*(volatile uint32_t *)(0x08000000u + (o)))
#define MASK 0x03ffffffu

#define KEY_BIT   (1u << 4)
#define POWER_BIT (1u << 3)
#define WATCH     (KEY_BIT | POWER_BIT)

#define BUCKETS     10u
#define BUCKET_TICKS 12000000u /* 1 s at 12 MHz */
#define EVENTS      1024u

struct event {
    uint32_t ticks;
    uint32_t input;
};

struct result {
    uint32_t magic, complete, hz, reserved0;
    uint32_t mux_before, dir_before, out_before, pull_before, in_before, timer_before;
    uint32_t pull_during, dir_during, dir_after, pull_after;
    uint32_t in_start, in_end, count, dropped, elapsed;
    uint32_t reserved1[3];
    uint32_t samples[BUCKETS];
    uint32_t high[BUCKETS];
    struct event events[EVENTS];
};

_Static_assert(sizeof(struct result) == (22u + 2u * BUCKETS) * 4u + EVENTS * 8u,
               "result size");

static volatile struct result *const p = (void *)0x32008000u;

static uint32_t timer_raw(void)
{
    return R(0x104) & MASK;
}

void stub_main(void)
{
    volatile uint32_t *w = (void *)p;
    uint32_t last_raw, elapsed = 0, input;

    for (unsigned i = 0; i < sizeof(*p) / 4; i++) w[i] = 0;
    p->magic = 0x334b464cu;
    p->hz = 12000000;
    p->mux_before = R(0x78);
    p->dir_before = R(0x7c);
    p->out_before = R(0x80);
    p->pull_before = R(0x9c);
    p->in_before = R(0xbc);
    p->timer_before = R(0x1c);

    /* Refuse a running timer or a pin assigned to RTCK. */
    if ((p->timer_before & (1u << 26)) || (p->mux_before & 2u)) return;

    /* Input mode, and the pad pull-down off. A set bit disables the pull. */
    R(0x7c) = p->dir_before | KEY_BIT;
    R(0x9c) = p->pull_before | KEY_BIT;
    p->dir_during = R(0x7c);
    p->pull_during = R(0x9c);

    R(0x1c) = MASK;
    R(0x1c) = MASK | (1u << 26) | (1u << 27);

    last_raw = timer_raw();
    input = R(0xbc);
    p->in_start = input;
    while (elapsed < BUCKETS * BUCKET_TICKS) {
        uint32_t now = timer_raw();
        uint32_t value, b;

        elapsed += (last_raw - now) & MASK;
        last_raw = now;
        value = R(0xbc);
        b = elapsed / BUCKET_TICKS;
        if (b >= BUCKETS) b = BUCKETS - 1u;
        p->samples[b]++;
        if (value & KEY_BIT) p->high[b]++;
        if ((input ^ value) & WATCH) {
            if (p->count < EVENTS) {
                p->events[p->count].ticks = elapsed;
                p->events[p->count].input = value;
                p->count++;
            } else {
                p->dropped++;
            }
            input = value;
        }
    }
    p->elapsed = elapsed;
    p->in_end = R(0xbc);

    R(0x9c) = p->pull_before;
    R(0x1c) = 1u << 28;
    R(0x1c) = p->timer_before & MASK;
    R(0x7c) = p->dir_before;
    p->dir_after = R(0x7c);
    p->pull_after = R(0x9c);
    p->complete = 1;
}
