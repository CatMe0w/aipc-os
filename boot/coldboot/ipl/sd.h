#ifndef IPL_SD_H
#define IPL_SD_H

#include <stdint.h>

/* Bring up the SD/MMC controller on MCI base 0x20020000 and run the
 * standard SD 2.0 init sequence (CMD0, CMD8, ACMD41, CMD2, CMD3, CMD7),
 * then switch to a fast transfer clock, attempt 4-bit bus via ACMD6, and
 * bind an L2 buffer for DMA reads.
 *
 * On success the driver remembers the SDHC flag (from OCR bit30) and the
 * RCA so subsequent reads use the right addressing mode.
 *
 * Returns 0 on success. Negative on failure; the value indicates which
 * stage failed (e.g. -8 = CMD8 mismatch, -41 = ACMD41 timeout).
 */
int sd_init(void);

/* Restore the SYSCTRL clock-gate, sharepin, and pad-control registers to
 * the values they held at sd_init() entry. Needed before falling back to
 * NAND, which shares pad config bits at SYSCTRL+0x74 with the MCI block.
 *
 * Safe to call even if sd_init() was never invoked or failed mid-way; the
 * driver only restores when it actually captured a baseline.
 */
void sd_release_pins(void);

/* Read one 512-byte block at logical block address `lba` into `dst`.
 * For SDHC cards lba is a block index; for SDSC cards the driver
 * converts to a byte address internally.
 *
 * `dst` must be 4-byte aligned.
 *
 * Returns 0 on success, negative on command or FIFO timeout.
 */
int sd_read_block(uint32_t lba, void *dst);

#endif
