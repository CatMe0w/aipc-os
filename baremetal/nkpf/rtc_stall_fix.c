/*
 * This patch makes the RTC read state machine in the OAL timer tick return at
 * once. When the RTC ready bit stays clear, its poll keeps the CPU in the IRQ
 * handler, and no thread runs. See README.md.
 */

#include "nkpf.h"

#define BOOTARGS_RTC_PRESENT 0xA0020848u
#define ARM_BX_LR            0xE12FFF1Eu
#define LDR_FLAG             6
#define POP_EARLY            13

static bool found;

static uint32_t ldr_literal(uint32_t *ldr)
{
    return *(uint32_t *)((uintptr_t)ldr + 8u + (*ldr & 0xFFFu));
}

static bool rtc_stall_fix_callback(struct pf_patch *patch, uint32_t *stream)
{
    /* The early return must pop the same registers that the entry pushed.
     * This check rejects functions that only look similar. */
    if ((stream[0] & 0xFFFFu) != (stream[POP_EARLY] & 0xFFFFu))
        return false;
    if (ldr_literal(stream + LDR_FLAG) != BOOTARGS_RTC_PRESENT)
        return false;
    if (found)
        panic("rtc_stall_fix: Found twice");
    found = true;
    stream[0] = ARM_BX_LR;
    printf("  RTC poll %p\n", pf_ptr_to_va(nkpf_range, stream));
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
        0xFFFFF000u, 0xFFFFF000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFF000000u, 0xFFFFF000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFF000u,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFF000u, 0xFFFFFFFFu,
    };

    pf_maskmatch(patchset, "rtc_stall_fix", matches, masks,
                 sizeof(matches) / sizeof(matches[0]), true,
                 rtc_stall_fix_callback);
}

nkpf_component_t nkpf_rtc_stall_fix = {
    .name = "rtc_stall_fix",
    .patches = {
        { "nk.exe", rtc_stall_fix_patch },
        { NULL, NULL },
    },
};
