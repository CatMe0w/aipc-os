#include "cfw.h"

uint32_t cfw_enable __attribute__((section(".data")));
enum cfw_stage cfw_stage;
pf_range_t *cfw_range;

/* cfw_run() destroys every patchset before the next patch, so a bump
 * allocator is enough. */
static uint8_t arena[4096] __attribute__((aligned(8)));
static uint32_t arena_used;

void *malloc(size_t size)
{
    void *p = &arena[arena_used];

    size = (size + 7u) & ~7u;
    if (size > sizeof(arena) - arena_used)
        panic("cfw: out of memory");
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

static bool has_stage(cfw_component_t *c, enum cfw_stage stage)
{
    for (cfw_patch_t *p = c->patches; p->patch; p++)
        if (p->stage == stage)
            return true;
    return false;
}

void cfw_run(enum cfw_stage stage,
             void (*range_of)(const char *module, pf_range_t *range))
{
    for (uint32_t i = 0; i < cfw_component_count; i++) {
        cfw_component_t *c = cfw_components[i];

        if (!(cfw_enable & (1u << i)) || !has_stage(c, stage))
            continue;
        printf("%s\n", c->info.id);
        if (c->init)
            c->init();
        for (cfw_patch_t *p = c->patches; p->patch; p++) {
            pf_range_t range;
            pf_patchset_t *set;

            if (p->stage != stage)
                continue;
            range_of(p->module, &range);
            set = pf_patchset_create();
            p->patch(set);
            cfw_range = &range;
            pf_apply(&range, set);
            cfw_range = NULL;
            pf_patchset_destroy(set);
            arena_used = 0;
        }
        if (c->finish)
            c->finish();
    }
}

void cfw_patch_once(struct pf_patch *patch)
{
    if (patch->has_fired)
        panic("%s: Found twice", patch->name);
    printf("  %s\n", patch->name);
}
