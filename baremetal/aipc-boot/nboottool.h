#pragma once
#include <stdint.h>

enum {
    NBOOTTOOL_OK = 0,
    NBOOTTOOL_ENAND,        /* block 0 could not be read */
    NBOOTTOOL_EBLOCK0,      /* block0.c rejects the block or the image */
    NBOOTTOOL_ECARD,
    NBOOTTOOL_ENOBACKUP,    /* /backup has no backup that fits block 0 */
    NBOOTTOOL_EDAMAGED,     /* the only backup does not have the checksum in its name */
    NBOOTTOOL_EAMBIGUOUS,   /* /backup has two that fit, with different contents */
    NBOOTTOOL_EWRITE,       /* the write failed. Block 0 is as it was before */
    NBOOTTOOL_EROLLBACK,    /* the write failed, and block 0 is not valid */
};

/* The card must be mounted for all of these. Only nboot_write() changes NAND.
 *
 * A prepare function reads block 0 and makes the image that nboot_write()
 * writes. nboot_prepare_install() takes the openNBOOT in this aipc-boot.
 * nboot_prepare_remove() takes the backup of the stock bootloader in /backup
 * that has the header page of block 0, and name gets its file name. */
int nboot_prepare_install(void);
int nboot_prepare_remove(char *name, uint32_t size);

/* Saves the block that the prepare function read to /backup, in the format
 * of `opennboot install`. name gets the file name, without the directory. */
int nboot_backup(char *name, uint32_t size);

/* Writes the prepared image to block 0 and verifies it. After a failure it
 * writes the block that the prepare function read back. */
int nboot_write(void);

/* The banner of the openNBOOT in this aipc-boot, in the form of
 * sysinfo_nboot_name(). */
const char *nboot_bundled_name(void);
