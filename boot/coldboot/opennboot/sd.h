#pragma once
#include <stdint.h>

/* SD 2.0 init sequence, 4-bit bus, L2 DMA reads. Returns 0 on success,
 * negative on failure (value indicates the failing stage). */
int sd_init(void);

/* Restore SYSCTRL registers to their sd_init()-entry values so the NAND
 * fallback path can reclaim the shared pads. Safe even if sd_init() failed. */
void sd_release_pins(void);

/* Read one 512-byte block. `dst` must be 4-byte aligned. */
int sd_read_block(uint32_t lba, void *dst);
