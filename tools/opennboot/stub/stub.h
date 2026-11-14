#pragma once
#include <stdint.h>
#include "nand.h"

/* Block image buffer, one NAND_PAGE_RAW slot per page. Known max block is 128 pages
 * (v1.88 device), so it ends at 0x30162000, clear of the log buffer at 0x301F0000. */
#define NF_BUF       0x30120000u

#define NF_MAX_PAGES 64u    /* one block 0 image. see MAX_PAYLOAD_PAGES on the host */

/* Batch buffer of nf_backup and nf_restore. It ends at 0x31840000. */
#define NF_BACKUP_BUF       0x31000000u
#define NF_BACKUP_MAX_PAGES 4096u

/* The codes of nand.h are negative. */
#define STUB_EARG 1

/* In USB boot mode the bootrom has not set up the NAND pins and clock. */
static inline void nf_hw_init(void)
{
    ((void (*)(void))0x2648u)();    /* nf_boot_hw_init of the bootrom */
    nand_init();
}
