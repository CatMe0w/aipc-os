#include "cfw.h"
#include "layout.h"
#include "log.h"

struct cfw_result *cfw_eboot_result;
static pf_range_t eboot_range;

void handoff_patch(pf_range_t *range);

/* EBOOT is one flat image, so every patch scans all of it. */
static void eboot_range_of(const char *module, pf_range_t *range)
{
    (void)module;
    *range = eboot_range;
}

int32_t eboot_main(const struct cfw_eboot_args *args,
                   struct cfw_result *result)
{
    cfw_stage = CFW_EBOOT;
    cfw_eboot_result = result;
    result->status = 0;
    result->message[0] = 0;
    log_init();
    printf("cfw: eboot stage\n");

    if (args->enable >> cfw_component_count)
        panic("Unknown components 0x%x", args->enable);
    cfw_enable = args->enable;
    eboot_range.va = EBOOT_CODE_VA;
    eboot_range.size = args->bytes & ~3u;
    eboot_range.cacheable_base = (uint8_t *)(uintptr_t)args->code;

    cfw_run(CFW_EBOOT, eboot_range_of);
    handoff_patch(&eboot_range);
    return 0;
}
