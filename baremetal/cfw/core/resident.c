#include "cfw.h"
#include "layout.h"

extern const uint8_t nk_resident_start[], nk_resident_end[];

void cfw_nk_resident_install(void)
{
    static bool installed;
    uint32_t bytes = (uint32_t)(nk_resident_end - nk_resident_start);

    if (installed)
        return;
    installed = true;
    memset((void *)NK_RESIDENT_PHYS, 0, NK_RESIDENT_MAX);
    memcpy((void *)NK_RESIDENT_PHYS, nk_resident_start, bytes);
    printf("  nk resident %p\n", NK_RESIDENT_VA);
}
