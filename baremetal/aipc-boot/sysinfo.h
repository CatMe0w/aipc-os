#pragma once

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

/* The banner of openNBOOT up to its newline, for example "openNBOOT", or
 * "Stock" or "Unknown". */
const char *sysinfo_nboot_name(void);

/* The release tag, for example "2026-08-21", or "dev" for a local build. */
extern const char aipc_boot_version[];
