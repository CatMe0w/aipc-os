/*
 * Patches to EBOOT in DDR, before the handoff.
 */

#include <stddef.h>
#include "eboot_patch.h"
#include "log.h"
#include "pf.h"

#define ALL 0xFFFFFFFFu

/*
 * Power hold. EBOOT rewrites all of GPIO4, including bit 9 (POWER_ON), and
 * drives bit 9 high again only later. In that interval, +5V drops unless the
 * user holds the power key. The patches keep bit 9 an output at 1. See
 * docs/eboot/boot-flow.md.
 */

/* v1.58.2 writes direction and output next to each other. */
static const uint32_t hold1582_match[] = {
    0xE3E03801u,  /* mvn r3, #0x10000    */
    0xE5802098u,  /* str r2, [r0, #0x98] */
    0xE5803094u,  /* str r3, [r0, #0x94] */
    0xE3E03000u,  /* mvn r3, #0          */
    0xE580307Cu,  /* str r3, [r0, #0x7c] */
    0xE3E03000u,  /* mvn r3, #0          */
};
static const uint32_t hold1582_mask[] = { ALL, ALL, ALL, ALL, ALL, ALL };
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
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
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

/*
 * The patch changes the jump to NK, `mov pc, r0`, to `ldr pc, [pc, #-4]`.
 * The next word is an unreachable `mov pc, lr`, and the patch writes the nkpf
 * address there. r0 still holds the NK entry when nkpf starts.
 */
static const uint32_t handoff_match[] = {
    0xE3A01070u,  /* mov r1, #0x70           */
    0xEE011F10u,  /* mcr p15, 0, r1, c1, c0  */
    0xE1A00000u,  /* nop                     */
    0xE1A0F002u,  /* mov pc, r2              */
    0xE1A00000u,  /* nop                     */
    0xE3A02000u,  /* mov r2, #0              */
    0xEE082F17u,  /* mcr p15, 0, r2, c8, c7  */
    0xE1A0F000u,  /* mov pc, r0              */
    0xE1A0F00Eu,  /* mov pc, lr              */
};
static const uint32_t handoff_mask[] = {
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
};
#define HANDOFF_JUMP     7
#define LDR_PC_PC_MINUS4 0xE51FF004u

static uint32_t nkpf_entry;

static void put_words(uint32_t *dst, const uint32_t *src, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        dst[i] = src[i];
}

static bool once(struct pf_patch *patch)
{
    if (patch->has_fired)
        panic("%s: Found twice", patch->name);
    log_puts("eboot: ");
    log_puts(patch->name);
    log_puts("\n");
    return true;
}

static bool hold1582_callback(struct pf_patch *patch, uint32_t *stream)
{
    once(patch);
    put_words(stream, hold1582_to, 6);
    return true;
}

static bool hold188_callback(struct pf_patch *patch, uint32_t *stream)
{
    once(patch);
    stream[HOLD188_DIR] = MVN_R3_0x200;
    put_words(stream + HOLD188_OUT, hold188_out_to, 5);
    return true;
}

static bool handoff_callback(struct pf_patch *patch, uint32_t *stream)
{
    once(patch);
    stream[HANDOFF_JUMP + 1] = nkpf_entry;
    stream[HANDOFF_JUMP] = LDR_PC_PC_MINUS4;
    return true;
}

void eboot_patch(uint32_t *code, uint32_t bytes, uint32_t nkpf)
{
    pf_range_t range = { EBOOT_CODE_VA, bytes, (uint8_t *)code };
    pf_patchset_t *set = pf_patchset_create();

    nkpf_entry = nkpf;
    pf_maskmatch(set, "power hold v1.58.2", hold1582_match, hold1582_mask,
                 6, false, hold1582_callback);
    pf_maskmatch(set, "power hold v1.88", hold188_match, hold188_mask,
                 21, false, hold188_callback);
    if (nkpf)
        pf_maskmatch(set, "handoff to nkpf", handoff_match, handoff_mask,
                     9, true, handoff_callback);
    pf_apply(&range, set);
    pf_patchset_destroy(set);
}
