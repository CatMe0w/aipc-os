#include <string.h>
#include "fbcon.h"
#include "layout.h"
#include "log.h"
#include "nkpf.h"

static nkpf_component_t *const components[] = {
    &nkpf_rtc_stall_fix,
#ifdef NKPF_DIAG
    &nkpf_diag,
#endif
};

pf_range_t *nkpf_range;

/* run() frees all patchsets after each component, so a bump allocator is
 * enough. */
static uint8_t arena[4096] __attribute__((aligned(8)));
static uint32_t arena_used;

void *malloc(size_t size)
{
    void *p = &arena[arena_used];

    size = (size + 7u) & ~7u;
    if (size > sizeof(arena) - arena_used)
        panic("nkpf: out of memory");
    arena_used += size;
    return p;
}

void free(void *p)
{
    (void)p;
}

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;

    while (n--)
        *p++ = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d;
    const uint8_t *q = s;

    while (n--)
        *p++ = *q++;
    return d;
}

static void icache(int on)
{
    uint32_t cr;

    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (cr));
    cr = on ? cr | (1u << 12) : cr & ~(1u << 12);
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r" (cr));
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r" (0));
}

static void run(nkpf_component_t *c)
{
    if (c->init)
        c->init();
    for (nkpf_patch_t *p = c->patches; p->patch; p++) {
        pf_range_t range;
        pf_patchset_t *set;

        if (!rom_code_range(p->module, &range))
            panic("No ROM module %s", p->module);
        set = pf_patchset_create();
        p->patch(set);
        nkpf_range = &range;
        pf_apply(&range, set);
        nkpf_range = 0;
        pf_patchset_destroy(set);
    }
    if (c->finish)
        c->finish();
    arena_used = 0;
}

void nkpf_main(uint32_t entry)
{
    icache(1);
    log_init();
    screen_init((volatile uint16_t *)FB_PHYS, FB_WIDTH, 0, 0, FB_WIDTH,
                FB_HEIGHT, 1, RGB565_BLACK, false);
    screen_set_color(RGB565_CYAN);
    printf("nkpf\n");
    screen_mark_banner();
    screen_set_color(RGB565_WHITE);
    printf("NK entry %p\n", entry);

    for (unsigned i = 0; i < sizeof(components) / sizeof(components[0]); i++) {
        printf("%s\n", components[i]->name);
        run(components[i]);
    }

    screen_set_color(RGB565_GREEN);
    printf("Booting NK\n");
    icache(0);
}
