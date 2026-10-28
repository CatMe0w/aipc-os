#include "cfw.h"

/* Both patches keep GPIO4 bit 9 (POWER_ON) an output at 1. Bit 9 of the
 * output register (0x98) must be 1 before bit 9 of the direction register
 * (0x94) makes the pin an output, or the pin drives low in between. */

/* v1.58.2 writes direction and output next to each other. */
static const uint32_t hold1582_match[] = {
    0xE3E03801u,  /* mvn r3, #0x10000    */
    0xE5802098u,  /* str r2, [r0, #0x98] */
    0xE5803094u,  /* str r3, [r0, #0x94] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE580307Cu,  /* str r3, [r0, #0x7c] */
    0xE3E03000u,  /* mvn r3, #0          */
};
static const uint32_t hold1582_mask[] = {
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
};
static const uint32_t hold1582_to[] = {
    0xE3822C02u,  /* orr r2, r2, #0x200  */
    0xE1E03002u,  /* mvn r3, r2          */
    0xE5802098u,  /* str r2, [r0, #0x98] */
    0xE5803094u,  /* str r3, [r0, #0x94] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE580307Cu,  /* str r3, [r0, #0x7c] */
};

/* v1.88 writes them 15 words apart. The pattern ends with the store to 0x98,
 * which the patch changes. Without that word, the pattern also matches once
 * in v1.58.2. */
static const uint32_t hold188_match[] = {
    0xE3E03000u,  /* mvn r3, #0          */
    0xE580308Cu,  /* str r3, [r0, #0x8c] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE3A02000u,  /* mov r2, #0          */
    0xE5803094u,  /* str r3, [r0, #0x94] */
    0xE58020E0u,  /* str r2, [r0, #0xe0] */
    0xE58020E4u,  /* str r2, [r0, #0xe4] */
    0xE58020E8u,  /* str r2, [r0, #0xe8] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE58020ECu,  /* str r2, [r0, #0xec] */
    0xE58030F0u,  /* str r3, [r0, #0xf0] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE58030F4u,  /* str r3, [r0, #0xf4] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE58030F8u,  /* str r3, [r0, #0xf8] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE58030FCu,  /* str r3, [r0, #0xfc] */
    0xE5802080u,  /* str r2, [r0, #0x80] */
    0xE5802088u,  /* str r2, [r0, #0x88] */
    0xE5802090u,  /* str r2, [r0, #0x90] */
    0xE5802098u,  /* str r2, [r0, #0x98] */
};
static const uint32_t hold188_mask[] = {
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
};
/* Direction: bit 9 stays an output. */
#define HOLD188_DIR     2
#define MVN_R3_0x200    0xE3E03C02u
/* Output: word 15 is redundant because r3 is still all ones. The patch uses
 * that word to set r2 = 0x200 for the store to 0x98. */
#define HOLD188_OUT     15
static const uint32_t hold188_out_to[] = {
    0xE58030FCu,  /* str r3, [r0, #0xfc] */
    0xE5802080u,  /* str r2, [r0, #0x80] */
    0xE5802088u,  /* str r2, [r0, #0x88] */
    0xE5802090u,  /* str r2, [r0, #0x90] */
    0xE3A02C02u,  /* mov r2, #0x200      */
};

static void put_words(uint32_t *dst, const uint32_t *src, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        dst[i] = src[i];
}

static bool found;

static bool hold1582_callback(struct pf_patch *patch, uint32_t *stream)
{
    cfw_patch_once(patch);
    found = true;
    put_words(stream, hold1582_to, ARRAY_SIZE(hold1582_to));
    return true;
}

static bool hold188_callback(struct pf_patch *patch, uint32_t *stream)
{
    cfw_patch_once(patch);
    found = true;
    stream[HOLD188_DIR] = MVN_R3_0x200;
    put_words(stream + HOLD188_OUT, hold188_out_to,
              ARRAY_SIZE(hold188_out_to));
    return true;
}

/* Each firmware version matches one of the two patterns, so neither pattern
 * is required alone. power_hold_finish() requires one of them. */
static void power_hold_patch(pf_patchset_t *set)
{
    pf_maskmatch(set, "power hold v1.58.2", hold1582_match, hold1582_mask,
                 ARRAY_SIZE(hold1582_match), false, hold1582_callback);
    pf_maskmatch(set, "power hold v1.88", hold188_match, hold188_mask,
                 ARRAY_SIZE(hold188_match), false, hold188_callback);
}

static void power_hold_finish(void)
{
    if (!found)
        panic("Missing patch: power hold");
}

cfw_component_t cfw_power_hold = {
    .info = {
        .id = "power_hold",
        .name = "Power hold",
        .description = "Keeps the device on while EBOOT starts, so you can let "
                       "go of the power key.",
        .flags = CFW_FIX | CFW_DEFAULT_ON,
    },
    .finish = power_hold_finish,
    .patches = {
        { CFW_EBOOT, NULL, power_hold_patch },
        { 0, NULL, NULL },
    },
};
