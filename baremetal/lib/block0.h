#pragma once
#include <stdint.h>
#include "nand.h"

/* An image of NAND block 0 holds BLOCK0_PAGES raw pages. Page 0 is the header
 * page that the bootrom reads, and the bootloader starts at page 1. See
 * docs/bootrom/nand-boot.md. */
#define BLOCK0_PAGES        64u
#define BLOCK0_BYTES        (BLOCK0_PAGES * NAND_PAGE_RAW)
#define BLOCK0_PAYLOAD_MAX  ((BLOCK0_PAGES - 1u) * NAND_PAGE_DATA)

/* The codes of nand.h end at -12. */
enum {
    BLOCK0_ESIGNATURE = -20,    /* the header page has no ANYKA382 signature */
    BLOCK0_EPAGESIZE  = -21,    /* the header declares a page size other than 2048 */
    BLOCK0_EPAGES     = -22,    /* the header declares more pages than the image holds */
    BLOCK0_ETOOLARGE  = -23,    /* the payload is empty or larger than BLOCK0_PAYLOAD_MAX */
    BLOCK0_EVERIFY    = -24,    /* the block does not read back as written */
};

/* Checks the header page of an image. The bootrom does not start an image
 * that fails this check. */
int block0_check(const uint8_t *image);

/* Builds the image that starts payload. The header page comes from current,
 * an image of the block as it is now, and only its page count changes. image
 * must not overlap current. */
int block0_build(const uint8_t *current, const void *payload, uint32_t len,
                 uint8_t *image);

enum block0_step {
    BLOCK0_CHECK,       /* nothing was erased */
    BLOCK0_ERASE,
    BLOCK0_PROGRAM,
    BLOCK0_READBACK,
    BLOCK0_COMPARE,
};

struct block0_result {
    uint32_t step;      /* enum block0_step, the step that failed */
    uint32_t status;    /* chip status byte of the last erase or program */
    uint32_t page;      /* in the block. Valid from BLOCK0_PROGRAM on */
    uint32_t column;    /* BLOCK0_COMPARE: the first byte that differs */
    uint32_t got, want;
};

/* Erases the block at page and programs image, then reads the block into
 * readback, a buffer of BLOCK0_BYTES, and compares. It programs the 2048 data
 * bytes of each page up to the last one that has data. The spare bytes of
 * image are not written, and they must read back as 0xFF.
 *
 * An image that fails block0_check() is not written. */
int block0_write(uint32_t page, const uint8_t *image, uint8_t *readback,
                 struct block0_result *res);
