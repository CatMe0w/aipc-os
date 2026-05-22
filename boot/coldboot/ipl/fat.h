#ifndef IPL_FAT_H
#define IPL_FAT_H

#include <stdint.h>

/* Locate `name11`, an 11-byte 8.3 short name, space padded, no dot, in
 * the root directory of the first FAT16/FAT32 partition on the SD card,
 * and load its contents into `dst`.
 *
 * Example: to find "BOOT.BIN", pass "BOOT    BIN" (4 spaces between BOOT
 * and BIN, total 11 bytes; not null-terminated).
 *
 * If the file is larger than `max_bytes` the call fails with -9 and the
 * destination is left in an indeterminate state. On success the file size
 * is written to `*out_size` when non-NULL; bytes past the file end in
 * `dst` are not touched.
 *
 * Requires sd_init() to have succeeded and sharepin to still be in MMC
 * mode. Reads are issued through sd_read_block.
 *
 * Returns 0 on success. Negative on failure:
 *   -1 MBR sector read failed
 *   -2 MBR signature mismatch
 *   -3 no FAT16/FAT32 partition in MBR table
 *   -4 BPB sector read failed
 *   -5 unsupported sector size (only 512 supported)
 *   -6 directory sector read failed
 *   -7 file not found
 *   -8 data/FAT sector read failed during file load
 *   -9 file exceeds max_bytes
 */
int fat_load_file(const char *name11, void *dst, uint32_t max_bytes,
                  uint32_t *out_size);

#endif
