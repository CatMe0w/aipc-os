#include "cfw.h"

#define BOOTARGS_RTC_PRESENT 0xA0020848u
#define ARM_BX_LR            0xE12FFF1Eu
#define LDR_FLAG             6
#define POP_EARLY            13

static bool rtc_stall_fix_callback(struct pf_patch *patch, uint32_t *stream)
{
    /* The early return must pop the same registers that the entry pushed.
     * This check rejects functions that only look similar. */
    if ((stream[0] & 0xFFFFu) != (stream[POP_EARLY] & 0xFFFFu))
        return false;
    if (*ldr_literal(stream + LDR_FLAG) != BOOTARGS_RTC_PRESENT)
        return false;
    cfw_patch_once(patch);
    stream[0] = ARM_BX_LR;
    printf("  RTC poll %p\n", pf_ptr_to_va(cfw_range, stream));
    return true;
}

static void rtc_stall_fix_patch(pf_patchset_t *patchset)
{
    static const uint32_t matches[] = {
        0xE92D4000u,  /* push {r4, ..., lr}   */
        0xE59F4000u,  /* ldr  r4, =rtc state  */
        0xE3A01000u,  /* mov  r1, #0          */
        0xE5943068u,  /* ldr  r3, [r4, #0x68] */
        0xE3530000u,  /* cmp  r3, #0          */
        0x0A000000u,  /* beq  out             */
        0xE59F3000u,  /* ldr  r3, =BOOTARGS   */
        0xE5933000u,  /* ldr  r3, [r3]        */
        0xE3530000u,  /* cmp  r3, #0          */
        0x059F2000u,  /* ldreq r2, =count     */
        0x05923000u,  /* ldreq r3, [r2]       */
        0x02833001u,  /* addeq r3, r3, #1     */
        0x05823000u,  /* streq r3, [r2]       */
        0x08BD4000u,  /* popeq {r4, ..., lr}  */
        0x012FFF1Eu,  /* bxeq lr              */
    };
    static const uint32_t masks[] = {
        0xFFFFF000u,  /* push {*, lr}  */
        0xFFFFF000u,  /* ldr  r4, =*   */
        EXACT,
        EXACT,
        EXACT,
        0xFF000000u,  /* beq  *        */
        0xFFFFF000u,  /* ldr  r3, =*   */
        EXACT,
        EXACT,
        0xFFFFF000u,  /* ldreq r2, =*  */
        EXACT,
        EXACT,
        EXACT,
        0xFFFFF000u,  /* popeq {*, lr} */
        EXACT,
    };

    pf_maskmatch(patchset, "rtc_stall_fix", matches, masks,
                 ARRAY_SIZE(matches), true, rtc_stall_fix_callback);
}

cfw_component_t cfw_rtc_stall_fix = {
    .info = {
        .id = "rtc_stall_fix",
        .name = "RTC stall fix",
        .description = "Stops WinCE from hanging after \"BackLight "
                       "Initializing\".",
        .flags = CFW_FIX | CFW_DEFAULT_ON,
    },
    .patches = {
        { CFW_NK, "nk.exe", rtc_stall_fix_patch },
        { 0, NULL, NULL },
    },
};
