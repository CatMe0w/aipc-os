#pragma once
#include <stdint.h>

enum nboot_kind {
    NBOOT_UNKNOWN,
    NBOOT_STOCK,
    NBOOT_OPENNBOOT,
};

/* Reads NAND. Call it before card_init(), because NAND and the SD card
 * share pins. */
void sysinfo_read(void);

/* The version string in EBOOT, for example "1.58.2", or NULL. */
const char *sysinfo_fw_version(void);

/* The bootloader in NAND block 0. */
enum nboot_kind sysinfo_nboot(void);

/* The bootloader in an image of block 0, of BLOCK0_BYTES in block0.h. */
enum nboot_kind sysinfo_image_kind(const uint8_t *image);

/* Takes image as the contents of block 0, after a write to it. */
void sysinfo_set_block0(const uint8_t *image);

/* The banner of openNBOOT up to its newline, for example "openNBOOT", or
 * "Stock" or "Unknown". */
const char *sysinfo_nboot_name(void);

/* The release tag, for example "2026-08-21", or "dev" for a local build. */
extern const char aipc_boot_version[];
