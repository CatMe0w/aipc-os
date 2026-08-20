#pragma once
#include <stdint.h>

/* Loads `name11` (11-byte 8.3 short name, e.g. "BOOT    BIN") from the root
 * directory of the first FAT16/FAT32 partition. Needs a successful sd_init().
 *
 * Returns 0 on success, or a negative code:
 *   -1..-4  MBR/BPB read or validation
 *   -5      unsupported geometry
 *   -6..-7  directory read / file not found
 *   -8      data read during file load
 *   -9      file exceeds max_bytes or is empty
 *   -10     cluster chain shorter than file size
 */
int fat_load_file(const char *name11, void *dst, uint32_t max_bytes,
                  uint32_t *out_size);
