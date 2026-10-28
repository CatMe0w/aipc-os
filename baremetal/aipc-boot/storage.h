#pragma once
#include <stdint.h>
#include "ff.h"

/* Initializes the card and mounts the volume. */
int storage_mount(void);

/* Reads the whole file at path, or path.new without path. An empty file, or a
 * file larger than max, fails with STORAGE_ESIZE. */
int storage_load(const char *path, void *dst, uint32_t max, uint32_t *size);

/* Reads len bytes at the position of f. f keeps a pointer to a static link
 * map, so no other open file may use this function until f is closed. */
int storage_read(FIL *f, void *dst, uint32_t len);

/* Replaces the file at path. A failure leaves the old file or a complete
 * path.new. */
int storage_save(const char *path, const void *src, uint32_t len);

/* Free and total space in MB, and the type, for example "FAT32". Without a
 * valid FSINFO sector, it reads the whole FAT. */
int storage_space(uint32_t *free_mb, uint32_t *total_mb, const char **type);

#define STORAGE_ENOTFOUND  (-30)
#define STORAGE_ESIZE      (-31)
#define STORAGE_EIO        (-32)
