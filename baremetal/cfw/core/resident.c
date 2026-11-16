#include "cfw.h"

extern const uint8_t eboot_resident_start[], eboot_resident_end[];
extern const uint8_t nk_resident_start[], nk_resident_end[];

static const struct {
    const char *name;
    const uint8_t *start, *end;
} residents[] = {
    [CFW_EBOOT] = { "eboot", eboot_resident_start, eboot_resident_end },
    [CFW_NK]    = { "nk", nk_resident_start, nk_resident_end },
};

void cfw_resident_install(enum cfw_stage stage)
{
    static bool installed[ARRAY_SIZE(residents)];

    if (installed[stage])
        return;
    installed[stage] = true;
    memset((void *)RESIDENT_PHYS, 0, RESIDENT_MAX);
    memcpy((void *)RESIDENT_PHYS, residents[stage].start,
           (size_t)(residents[stage].end - residents[stage].start));
    printf("  %s resident %p\n", residents[stage].name, RESIDENT_VA);
}
