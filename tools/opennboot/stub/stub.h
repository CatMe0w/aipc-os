#pragma once
#include <stdint.h>
#include "nand.h"

/* Buffers of nf_block0, one block 0 image each. The host fills and reads
 * them. They lie in the batch buffer, which only the other two stubs use. */
#define B0_CURRENT   0x31000000u    /* block 0 as nf_block0 read it */
#define B0_IMAGE     0x31040000u    /* the image to write */
#define B0_READBACK  0x31080000u
#define B0_PAYLOAD   0x310C0000u    /* the bootloader that B0_BUILD installs */

/* Batch buffer of nf_backup and nf_restore. It ends at 0x31840000. */
#define NF_BACKUP_BUF       0x31000000u
#define NF_BACKUP_MAX_PAGES 4096u

/* The codes of nand.h and block0.h are negative. */
#define STUB_EARG 1

/* In USB boot mode the bootrom has not set up the NAND pins and clock. */
static inline void nf_hw_init(void)
{
    ((void (*)(void))0x2648u)();    /* nf_boot_hw_init of the bootrom */
    nand_init();
}
