/*
 * NK+0x40 holds "ECEC", and NK+0x44 holds the VA of the ROMHDR. The module
 * table follows the ROMHDR. A section can execute at a slot address (o32
 * realaddr) that differs from where it is stored (o32 dataptr).
 */

#include "layout.h"
#include "nkpf.h"

#define ECEC_MAGIC      0x43454345u
#define ROMHDR_BYTES    0x54u
#define TOC_BYTES       32u
#define O32_BYTES       24u
#define SCN_CODE        0x00000020u
#define SCN_COMPRESSED  0x00002000u

/* ROMHDR word 4 */
#define HDR_NUMMODS     4
/* TOC entry words */
#define TOC_NAME        4
#define TOC_E32         5
#define TOC_O32         6
/* o32 entry words */
#define O32_VSIZE       0
#define O32_PSIZE       2
#define O32_DATAPTR     3
#define O32_REALADDR    4
#define O32_FLAGS       5

bool nk_static_va(uint32_t va, uint32_t bytes)
{
    return va >= 0x80000000u && va < 0x88000000u &&
           bytes <= 0x88000000u - va;
}

void *nk_ptr(uint32_t va)
{
    return (void *)(uintptr_t)(va - NK_VA_OFFSET);
}

static bool name_is(uint32_t va, const char *name)
{
    const char *s;

    if (!nk_static_va(va, 1))
        return false;
    s = nk_ptr(va);
    for (; *name; s++, name++) {
        char c = *s;

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != *name)
            return false;
    }
    return *s == 0;
}

static const uint32_t *romhdr(void)
{
    const uint32_t *nk = nk_ptr(0x80200000u);
    uint32_t hdr = nk[0x44 / 4];

    if (nk[0x40 / 4] != ECEC_MAGIC)
        panic("No ECEC signature in NK");
    if (!nk_static_va(hdr, ROMHDR_BYTES))
        panic("Bad ROMHDR pointer %p", hdr);
    return nk_ptr(hdr);
}

bool rom_code_range(const char *module, pf_range_t *range)
{
    const uint32_t *hdr = romhdr();
    uint32_t n = hdr[HDR_NUMMODS];
    uint32_t toc_va = (uint32_t)(uintptr_t)hdr + NK_VA_OFFSET + ROMHDR_BYTES;
    const uint32_t *toc;

    if (n > 512 || !nk_static_va(toc_va, n * TOC_BYTES))
        panic("Bad ROM module table");
    toc = nk_ptr(toc_va);
    for (uint32_t i = 0; i < n; i++, toc += TOC_BYTES / 4) {
        const uint32_t *o32;
        const uint32_t *code = 0;
        uint32_t nobj;

        if (!name_is(toc[TOC_NAME], module))
            continue;
        if (!nk_static_va(toc[TOC_E32], 2))
            panic("%s: bad e32", module);
        nobj = *(const uint16_t *)nk_ptr(toc[TOC_E32]);
        if (nobj > 32 || !nk_static_va(toc[TOC_O32], nobj * O32_BYTES))
            panic("%s: bad o32", module);
        o32 = nk_ptr(toc[TOC_O32]);
        for (uint32_t j = 0; j < nobj; j++, o32 += O32_BYTES / 4) {
            if (!(o32[O32_FLAGS] & SCN_CODE))
                continue;
            if (code)
                panic("%s: two code sections", module);
            code = o32;
        }
        if (!code)
            panic("%s: no code section", module);
        if ((code[O32_FLAGS] & SCN_COMPRESSED) ||
            code[O32_VSIZE] > code[O32_PSIZE] ||
            !nk_static_va(code[O32_DATAPTR], code[O32_VSIZE]))
            panic("%s: code section not in place", module);
        range->va = code[O32_REALADDR];
        range->size = code[O32_VSIZE] & ~3u;
        range->cacheable_base = nk_ptr(code[O32_DATAPTR]);
        return true;
    }
    return false;
}
