#include <string.h>
#include "layout.h"
#include "nkpf.h"
#include "resident/resident.h"

extern const uint8_t resident_start[], resident_end[];

static struct resident_hdr *res;
static bool irq_found, ioctl_found;

void *resident_install(void)
{
    uint32_t bytes = (uint32_t)(resident_end - resident_start);

    if (res)
        return res;
    if (bytes > RESIDENT_MAX)
        panic("resident: image too large");
    memset((void *)RESIDENT_PHYS, 0, RESIDENT_MAX);
    memcpy((void *)RESIDENT_PHYS, resident_start, bytes);
    res = (struct resident_hdr *)RESIDENT_PHYS;
    if (res->magic != RESIDENT_MAGIC)
        panic("resident: bad magic");
    printf("  resident %p\n", RESIDENT_VA);
    return res;
}

static uint32_t *ldr_literal(uint32_t *ldr)
{
    return (uint32_t *)((uintptr_t)ldr + 8u + (*ldr & 0xFFFu));
}

static void res_word(uint32_t va, uint32_t value)
{
    *(uint32_t *)nk_ptr(va) = value;
}

/* Word 11 loads the address of OEMInterruptHandler from a literal. The patch
 * replaces that literal with irq_hook. */
#define IRQ_LDR_IP 11

static bool irq_callback(struct pf_patch *patch, uint32_t *stream)
{
    uint32_t *lit = ldr_literal(stream + IRQ_LDR_IP);

    if (!nk_static_va(*lit, 4))
        return false;
    if (irq_found)
        panic("irq: Found twice");
    irq_found = true;
    res_word(res->irq_orig, *lit);
    printf("  irq handler %p\n", *lit);
    *lit = res->irq_hook;
    return true;
}

static void irq_patch(pf_patchset_t *patchset)
{
    static const uint32_t matches[] = {
        0xE24EE004u,  /* sub  lr, lr, #4      */
        0xE92D500Fu,  /* push {r0-r3, ip, lr} */
        0xE28E0000u,  /* add  r0, lr, #imm    */
        0xE3500080u,  /* cmp  r0, #0x80       */
        0x3B000000u,  /* blcc imm             */
        0xE14F1000u,  /* mrs  r1, spsr        */
        0xE92D0002u,  /* push {r1}            */
        0xE1A0000Eu,  /* mov  r0, lr          */
        0xE321F093u,  /* msr  cpsr_c, #0x93   */
        0xE92D4000u,  /* push {lr}            */
        0xE92D0001u,  /* push {r0}            */
        0xE59FC000u,  /* ldr  ip, [pc, #imm]  */
        0xE1A0E00Fu,  /* mov  lr, pc          */
        0xE12FFF1Cu,  /* bx   ip              */
    };
    static const uint32_t masks[] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFF000u, 0xFFFFFFFFu, 0xFF000000u,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFF000u, 0xFFFFFFFFu, 0xFFFFFFFFu,
    };

    pf_maskmatch(patchset, "irq", matches, masks,
                 sizeof(matches) / sizeof(matches[0]), true, irq_callback);
}

/* Other functions have the same prologue as OEMIoControl. The callback
 * therefore requires the literal to point to a table whose first entry is a
 * HAL IOCTL code. */
#define IOCTL_LDR_TABLE 6

static bool ioctl_callback(struct pf_patch *patch, uint32_t *stream)
{
    uint32_t table = *ldr_literal(stream + IOCTL_LDR_TABLE);
    uint32_t entry = pf_ptr_to_va(nkpf_range, stream);

    if (!nk_static_va(table, 4) ||
        (*(uint32_t *)nk_ptr(table) & 0xFFFF0000u) != 0x01010000u)
        return false;
    if (ioctl_found)
        panic("ioctl: Found twice");
    ioctl_found = true;
    res_word(res->ioctl_resume, entry + 4u);
    stream[0] = 0xEA000000u | (((res->ioctl_hook - entry - 8u) >> 2) & 0x00FFFFFFu);
    printf("  OEMIoControl %p\n", entry);
    return true;
}

static void ioctl_patch(pf_patchset_t *patchset)
{
    static const uint32_t matches[] = {
        0xE92D4FF0u,  /* push {r4-r11, lr}   */
        0xE24DD008u,  /* sub  sp, sp, #8     */
        0xE1A09003u,  /* mov  r9, r3         */
        0xE1A0A002u,  /* mov  sl, r2         */
        0xE1A0B001u,  /* mov  fp, r1         */
        0xE1A07000u,  /* mov  r7, r0         */
        0xE59F6000u,  /* ldr  r6, =table     */
        0xE3A08000u,  /* mov  r8, #0         */
        0xE3A05000u,  /* mov  r5, #0         */
        0xE596E008u,  /* ldr  lr, [r6, #8]   */
    };
    static const uint32_t masks[] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFF000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    };

    pf_maskmatch(patchset, "ioctl", matches, masks,
                 sizeof(matches) / sizeof(matches[0]), true, ioctl_callback);
}

static void diag_init(void)
{
    resident_install();
}

nkpf_component_t nkpf_diag = {
    .name = "diag",
    .init = diag_init,
    .patches = {
        { "nk.exe", irq_patch },
        { "nk.exe", ioctl_patch },
        { NULL, NULL },
    },
};
