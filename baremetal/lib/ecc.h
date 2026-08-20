#pragma once
#include <stdint.h>

/* Decodes the error positions that the BCH engine reports. No MMIO, so a host
 * can unit-test it. */

int ecc_locate(uint32_t reg, uint32_t *byte_out, uint8_t *mask_out);

/* Returns 0 if this applied at least one correction and every position was
 * inside the chunk. Returns -1 in every other case. */
int ecc_apply(const uint32_t *regs, uint32_t n,
              uint8_t *data, uint8_t *tag, uint32_t tag_len);
