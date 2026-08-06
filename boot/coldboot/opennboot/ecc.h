#pragma once
#include <stdint.h>

/* BCH engine error position decoding, MMIO-free for host unit testing. */

int ecc_locate(uint32_t reg, uint32_t *byte_out, uint8_t *mask_out);

/* Returns 0 if at least one correction was applied and all positions landed
 * inside the chunk; -1 otherwise. */
int ecc_apply(const uint32_t *regs, uint32_t n,
              uint8_t *data, uint8_t *tag, uint32_t tag_len);
