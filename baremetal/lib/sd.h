#pragma once
#include <stdint.h>

/* SD 2.0 init sequence, 4-bit bus, L2 DMA reads. Returns 0 on success, or a
 * negative code that names the stage that failed. */
int sd_init(void);

/* Restores the SYSCTRL registers to their sd_init() entry values, so the NAND
 * fallback path can take the shared pads back. Safe after a failed sd_init(). */
void sd_release_pins(void);

/* Read one 512-byte block. `dst` must be 4-byte aligned. */
int sd_read_block(uint32_t lba, void *dst);
