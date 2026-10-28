#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pf.h"

/* nk_static_va() checks that the range is in the static DDR mapping of WinCE.
 * nk_ptr() returns the address where nkpf can access that VA now. */
bool nk_static_va(uint32_t va, uint32_t bytes);
void *nk_ptr(uint32_t va);

/* range.va is the execution address. For a process, this is a slot address,
 * which differs from the address where the code is stored. */
bool rom_code_range(const char *module, pf_range_t *range);
