#include "cfw.h"

cfw_component_t *const cfw_components[] = {
    &cfw_power_hold,
    &cfw_battery_check_skip,
    &cfw_rtc_stall_fix,
    &cfw_diag,
};

#define COMPONENT_COUNT ARRAY_SIZE(cfw_components)

const uint32_t cfw_component_count = COMPONENT_COUNT;

__attribute__((section(".header"), used))
const struct cfw_header cfw_header = {
    .eboot_stage = eboot_stage,
    .component_count = COMPONENT_COUNT,
    .components = (const struct cfw_component_info *const *)cfw_components,
};
