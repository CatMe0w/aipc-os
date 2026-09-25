#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pf.h"

typedef const struct {
    const char *module;                     /* ROM module, e.g. "nk.exe" */
    void (*patch)(pf_patchset_t *patchset); /* NULL ends the list */
} nkpf_patch_t;

typedef const struct {
    const char *name;
    void (*init)(void);
    void (*finish)(void);
    nkpf_patch_t patches[];
} nkpf_component_t;

extern nkpf_component_t nkpf_rtc_stall_fix;
extern nkpf_component_t nkpf_diag;

extern pf_range_t *nkpf_range;

void printf(const char *fmt, ...);

/* nk_static_va() checks that the range is in the static DDR mapping of WinCE.
 * nk_ptr() returns the address where nkpf can access that VA now. */
bool nk_static_va(uint32_t va, uint32_t bytes);
void *nk_ptr(uint32_t va);

/* range.va is the execution address. For a process, this is a slot address,
 * which differs from the address where the code is stored. */
bool rom_code_range(const char *module, pf_range_t *range);

void *resident_install(void);
