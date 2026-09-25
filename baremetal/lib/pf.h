/*
 * pongoOS - https://checkra.in
 *
 * Copyright (C) 2019-2023 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Changes: 32-bit mask matching only, over one flat range. No JIT, no
 * Mach-O ranges. xnu_pf_ is pf_.
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

struct pf_patchset;

typedef struct pf_patch {
    bool (*pf_callback)(struct pf_patch *patch, uint32_t *cacheable_stream);
    bool is_required;
    bool has_fired;
    bool should_match;
    void (*pf_match)(struct pf_patch *patch, uint32_t *preread,
                     uint32_t *cacheable_stream);
    struct pf_patch *next_patch;
    const char *name;
} pf_patch_t;

typedef struct pf_patchset {
    pf_patch_t *patch_head;
    bool is_required;
} pf_patchset_t;

typedef struct pf_range {
    uint32_t va;               /* where the code executes */
    uint32_t size;
    uint8_t *cacheable_base;   /* where the caller can access it now */
} pf_range_t;

/* The environment supplies malloc(), free() and panic(). */
void panic(const char *fmt, ...) __attribute__((noreturn));

pf_patchset_t *pf_patchset_create(void);
pf_patch_t *pf_maskmatch(pf_patchset_t *patchset, const char *name,
                         const uint32_t *matches, const uint32_t *masks,
                         uint32_t entryc, bool required,
                         bool (*callback)(struct pf_patch *patch,
                                          uint32_t *cacheable_stream));
void pf_apply(pf_range_t *range, pf_patchset_t *patchset);
void pf_disable_patch(pf_patch_t *patch);
void pf_enable_patch(pf_patch_t *patch);
void pf_patchset_destroy(pf_patchset_t *patchset);

static inline uint32_t pf_ptr_to_va(const pf_range_t *range, const void *p)
{
    return range->va + (uint32_t)((const uint8_t *)p - range->cacheable_base);
}
