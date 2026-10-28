#include "cfw.h"

#define MOV_R0_1  0xE3A00001u
#define BX_LR     0xE12FFF1Eu

static const uint32_t check_match[] = {
    0xE92D4030u,  /* push {r4, r5, lr}      */
    0xE3A04003u,  /* mov  r4, #3            */
    0xEB000000u,  /* bl   AC_DET pin        */
    0xEB000000u,  /* bl   GPIO read         */
    0xE3500000u,  /* cmp  r0, #0            */
    0x1A000000u,  /* bne  good              */
    0xEB000000u,  /* bl   battery voltage   */
    0xE3A05000u,  /* mov  r5, #threshold    */
    0xE3855001u,  /* orr  r5, r5, #1        */
    0xE1500005u,  /* cmp  r0, r5            */
    0xCA000000u,  /* bgt  good              */
};
static const uint32_t check_mask[] = {
    EXACT,
    EXACT,
    0xFF000000u,  /* bl   *        */
    0xFF000000u,  /* bl   *        */
    EXACT,
    0xFF000000u,  /* bne  *        */
    0xFF000000u,  /* bl   *        */
    0xFFFFF000u,  /* mov  r5, #*   */
    EXACT,
    EXACT,
    0xFF000000u,  /* bgt  *        */
};

static bool check_callback(struct pf_patch *patch, uint32_t *stream)
{
    cfw_patch_once(patch);
    /* 1 is "battery good". The caller ignores the result. */
    stream[0] = MOV_R0_1;
    stream[1] = BX_LR;
    return true;
}

static void battery_check_skip_patch(pf_patchset_t *set)
{
    pf_maskmatch(set, "battery check", check_match, check_mask,
                 ARRAY_SIZE(check_match), true, check_callback);
}

cfw_component_t cfw_battery_check_skip = {
    .info = {
        .id = "battery_check_skip",
        .name = "Skip battery check",
        .description = "Stops EBOOT from turning the device off after a false "
                       "low battery reading.",
        .flags = CFW_FIX | CFW_DEFAULT_ON,
    },
    .patches = {
        { CFW_EBOOT, NULL, battery_check_skip_patch },
        { 0, NULL, NULL },
    },
};
