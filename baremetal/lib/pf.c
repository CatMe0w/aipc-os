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
 * Changes: see pf.h.
 */

#include <stdlib.h>
#include <string.h>
#include "pf.h"

pf_patchset_t *pf_patchset_create(void)
{
    pf_patchset_t *r = malloc(sizeof(pf_patchset_t));
    if (r) {
        memset(r, 0, sizeof(pf_patchset_t));
        r->is_required = true;
    }
    return r;
}

struct pf_maskmatch {
    pf_patch_t patch;
    uint32_t pair_count;
    uint32_t pairs[][2];
};

static inline bool pf_maskmatch_match_32(struct pf_maskmatch *patch,
                                         uint32_t *preread,
                                         uint32_t *cacheable_stream)
{
    uint32_t count = patch->pair_count;
    for (uint32_t i = 0; i < count; i++) {
        if (count < 8) {
            if ((preread[i] & patch->pairs[i][1]) != patch->pairs[i][0])
                return false;
        } else {
            if ((cacheable_stream[i] & patch->pairs[i][1]) != patch->pairs[i][0])
                return false;
        }
    }
    return true;
}

static void pf_maskmatch_match(struct pf_patch *p, uint32_t *preread,
                               uint32_t *cacheable_stream)
{
    struct pf_maskmatch *patch = (struct pf_maskmatch *)p;

    if (pf_maskmatch_match_32(patch, preread, cacheable_stream)) {
        if (patch->patch.pf_callback(&patch->patch, cacheable_stream))
            patch->patch.has_fired = true;
    }
}

pf_patch_t *pf_maskmatch(pf_patchset_t *patchset, const char *name,
                         const uint32_t *matches, const uint32_t *masks,
                         uint32_t entryc, bool required,
                         bool (*callback)(struct pf_patch *patch,
                                          uint32_t *cacheable_stream))
{
    // Sanity check
    for (uint32_t i = 0; i < entryc; i++) {
        if ((matches[i] & masks[i]) != matches[i])
            panic("Bad maskmatch: %s (index %u)", name, i);
    }

    struct pf_maskmatch *mm = malloc(sizeof(struct pf_maskmatch) + 8 * entryc);
    memset(mm, 0, sizeof(struct pf_maskmatch));
    mm->patch.should_match = true;
    mm->patch.pf_callback = callback;
    mm->patch.pf_match = pf_maskmatch_match;
    mm->patch.is_required = required;
    mm->patch.name = name;
    mm->pair_count = entryc;

    for (uint32_t i = 0; i < entryc; i++) {
        mm->pairs[i][0] = matches[i];
        mm->pairs[i][1] = masks[i];
    }

    mm->patch.next_patch = patchset->patch_head;
    patchset->patch_head = &mm->patch;
    return &mm->patch;
}

void pf_disable_patch(pf_patch_t *patch)
{
    patch->should_match = false;
}

void pf_enable_patch(pf_patch_t *patch)
{
    patch->should_match = true;
}

/* Like upstream, this reads 8 words ahead, so it can read up to 32 bytes past
 * the end of the range. */
static void pf_apply_32(pf_range_t *range, pf_patchset_t *patchset)
{
    uint32_t *stream = (uint32_t *)range->cacheable_base;
    uint32_t reads[8];
    uint32_t stream_iters = range->size >> 2;
    for (int i = 0; i < 8; i++)
        reads[i] = stream[i];
    for (uint32_t index = 0; index < stream_iters; index++) {
        pf_patch_t *patch = patchset->patch_head;

        while (patch) {
            if (patch->should_match)
                patch->pf_match(patch, reads, &stream[index]);
            patch = patch->next_patch;
        }

        for (int i = 0; i < 7; i++)
            reads[i] = reads[i + 1];
        reads[7] = stream[index + 8];
    }
}

void pf_apply(pf_range_t *range, pf_patchset_t *patchset)
{
    pf_apply_32(range, patchset);
    if (patchset->is_required) {
        for (pf_patch_t *patch = patchset->patch_head; patch;
             patch = patch->next_patch) {
            if (patch->is_required && !patch->has_fired)
                panic("Missing patch: %s", patch->name);
        }
    }
}

void pf_patchset_destroy(pf_patchset_t *patchset)
{
    pf_patch_t *o_patch;
    pf_patch_t *patch = patchset->patch_head;
    while (patch) {
        o_patch = patch;
        patch = patch->next_patch;
        free(o_patch);
    }
    free(patchset);
}
