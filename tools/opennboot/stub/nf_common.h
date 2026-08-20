#pragma once
#include <stdint.h>

#define NF_PAGE_DATA 2048u      /* what the bootrom reads back, and all we program */
#define NF_PAGE_RAW  2112u      /* the whole physical page. read captures it, write does not */
#define NF_CHUNK     512u       /* forced by L2 buffer 5's size */

/* Block image buffer, one NF_PAGE_RAW slot per page. Known max block is 128 pages
 * (v1.88 device), so it ends at 0x30162000, clear of the log buffer at 0x301F0000. */
#define NF_BUF       0x30120000u

#define NF_MAX_PAGES 64u    /* one block 0 image. see MAX_PAYLOAD_PAGES on the host */

enum {
    NF_OK      = 0,
    NF_E_SEQ   = 1,     /* sequencer never reported done */
    NF_E_L2    = 2,     /* L2 buffer never reached the expected fill */
    NF_E_DMA   = 3,     /* DMA never reported done */
    NF_E_FAIL  = 4,     /* chip reported a failure status. CMD_FAIL says where */
    NF_E_ARG   = 5,
};

void nf_hw_init(void);
int  nf_read_id(uint32_t *out);
int  nf_read_page(uint32_t row, volatile uint8_t *dst);
int  nf_erase_block(uint32_t row, uint32_t *status);
int  nf_program_page(uint32_t row, const volatile uint8_t *src, uint32_t *status);
