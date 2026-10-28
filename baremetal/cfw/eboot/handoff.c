#include "cfw.h"

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
    EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT, EXACT,
};
#define HANDOFF_JUMP     7
#define LDR_PC_PC_MINUS4 0xE51FF004u

extern uint8_t nk_entry[];

/* The jump to NK, `mov pc, r0`, becomes `ldr pc, [pc, #-4]`. The next word
 * is an unreachable `mov pc, lr`, and it becomes the address of nk_entry.
 * r0 still holds the NK entry when nk_entry runs. */
static bool handoff_callback(struct pf_patch *patch, uint32_t *stream)
{
    cfw_patch_once(patch);
    stream[HANDOFF_JUMP + 1] = (uint32_t)(uintptr_t)nk_entry;
    stream[HANDOFF_JUMP] = LDR_PC_PC_MINUS4;
    return true;
}

/* Not a component: without this patch, the nk stage never runs. */
void handoff_patch(pf_range_t *range)
{
    pf_patchset_t *set = pf_patchset_create();

    pf_maskmatch(set, "handoff to nkpf", handoff_match, handoff_mask,
                 ARRAY_SIZE(handoff_match), true, handoff_callback);
    cfw_range = range;
    pf_apply(range, set);
    cfw_range = NULL;
    pf_patchset_destroy(set);
}
