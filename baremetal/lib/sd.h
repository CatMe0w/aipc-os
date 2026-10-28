#pragma once
#include <stdint.h>

/* SD 2.0 init sequence, 4-bit bus, transfers through the L2 buffer. Returns 0
 * on success, or a negative code that names the stage that failed. */
int sd_init(void);

/* Restores the SYSCTRL registers to their sd_init() entry values, so the NAND
 * fallback path can take the shared pads back. Safe after a failed sd_init(). */
void sd_release_pins(void);

/* Read one 512-byte block. `dst` must be 4-byte aligned. */
int sd_read_block(uint32_t lba, void *dst);

/* Writes one 512-byte block and waits until the card has programmed it.
 * `src` must be 4-byte aligned. */
int sd_write_block(uint32_t lba, const void *src);

/* Reads n consecutive blocks into dst with CMD23, CMD18 and L2 DMA. dst must
 * be 64-byte aligned, and no line of dst may be in the D-cache. Returns 0, or
 * a negative code. A failed transfer can leave state that corrupts the next
 * one, so call sd_init() before the next transfer after a failure.
 * SD_ENOCMD23 is the exception: the card rejected CMD23 before any data
 * moved, so no sd_init() is necessary. CMD23 is optional before UHS-I. */
#define SD_BLOCKS_MAX 1024u
#define SD_ENOCMD23   (-23)
int sd_read_blocks(uint32_t lba, uint32_t n, void *dst);

/* Selects high speed with CMD6 and raises the bus clock to 31 MHz. The card
 * returns to 20.67 MHz at the next sd_init(). Returns 0, or a negative code,
 * and the clock then stays at 20.67 MHz. */
int sd_high_speed(void);

/* Reads the card detect switch. Returns 1 while a card is in the slot. */
int sd_card_present(void);
