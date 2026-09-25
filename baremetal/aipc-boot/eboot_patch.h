#pragma once
#include <stdint.h>

/* EBOOT executes its image at VA = PA + 0x50000000. */
#define EBOOT_CODE_VA 0x80038000u

/* Patches the EBOOT code at [code, code + bytes) in DDR. With nkpf = 0 the
 * handoff stays as it is. */
void eboot_patch(uint32_t *code, uint32_t bytes, uint32_t nkpf);
