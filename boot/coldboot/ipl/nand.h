#ifndef IPL_NAND_H
#define IPL_NAND_H

#include <stdint.h>

/* Reads one NAND page with hardware ECC into `dst` (page_size bytes).
 * `oob_dst` is a small caller-allocated buffer (>= 32 bytes) used internally
 * by per-chunk ECC correction.
 *
 * Returns 0 on success. 3 on uncorrectable ECC error. 1 if a correctable
 * error was detected but software correction is not yet implemented.
 *
 * Geometry comes from the variables at 0x30E00D00..D13 which on dev path
 * are populated by usbboot_run.py's nand_init() and on prod path are set
 * by nboot's nboot_init_nand_params().
 */
int nand_read_page(uint32_t page_no, void *dst, void *oob_dst);

/* Read `max_bytes` from NAND starting at `start_block`, page by page,
 * into `dst`. Stops when `max_bytes` is fully read or any page read fails.
 *
 * Returns:
 *   0 on success (all `max_bytes` loaded).
 *   non-zero on first failed page read (propagated from nand_read_page).
 *
 * `max_bytes` should be a multiple of NAND_PAGE_SIZE for clean termination;
 * trailing remainder less than one page is discarded.
 *
 * No bad-block skip yet: blocks are walked in strict block-number order.
 */
int nand_load_partition(void *dst, uint32_t start_block, uint32_t max_bytes);

#endif
