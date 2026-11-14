#pragma once
#include <stdint.h>

enum {
    NAND_OK             =  0,
    NAND_ESEQ_CMD       = -1,
    NAND_ESEQ_DATA      = -2,
    NAND_EL2_FILL       = -3,
    NAND_EDMA_DONE      = -4,
    NAND_EUNCORRECTABLE = -5,
    NAND_EBADPOS        = -6,
    NAND_ENOGEOM        = -7,
    NAND_EPROBE         = -8,
    NAND_ELEN           = -9,
    NAND_EBADRUN        = -10,
    NAND_ESTATUS        = -11,  /* the chip reported a failed erase or program */
    NAND_EARG           = -12,
};

#define NAND_PAGE_DATA  2048u
#define NAND_PAGE_RAW   2112u   /* with the 64 spare bytes */

void nand_init(void);
int nand_probe_geometry(void);
int nand_load_image(void *dst, uint32_t start_block, uint32_t bytes);

/* Reads len bytes from column col of page, with no ECC. len is a multiple of
 * 4 and at most 512. Block 0, which the bootrom reads, has no ECC. */
int nand_read_raw(uint32_t page, uint32_t col, void *dst, uint32_t len);

/* Reads all NAND_PAGE_RAW bytes of a page, with no ECC. */
int nand_read_page_raw(uint32_t page, void *dst);

/* The first four bytes of READ ID, the manufacturer in bits [7:0]. */
int nand_read_id(uint32_t *id);

/* Decodes the value of nand_read_id(). It fails for a part that this driver
 * cannot address: a page other than NAND_PAGE_DATA bytes, or a capacity
 * outside 256 MB to 1 GB. */
int nand_id_geometry(uint32_t id, uint32_t *pages, uint32_t *pages_per_block);

/* page is the first page of a block. The marker is the byte at physical
 * column 0x411, see docs/nboot/boot-flow.md. Block 0 has no marker. */
int nand_block_bad(uint32_t page);

/* Erases the block that holds page. status gets the chip status byte. */
int nand_erase_block(uint32_t page, uint32_t *status);

/* Programs the first len bytes of a page with no ECC. len is NAND_PAGE_DATA
 * or NAND_PAGE_RAW. The columns after len keep the 0xFF of the erase. */
int nand_program_page(uint32_t page, const void *src, uint32_t len,
                      uint32_t *status);

/* Reads count pages into dst, NAND_PAGE_RAW bytes each, with no ECC. done
 * gets the number of pages read. */
int nand_read_pages_raw(uint32_t page, uint32_t count, void *dst, uint32_t *done);

enum nand_write_mode {
    NAND_WRITE,
    NAND_SCAN,      /* reports the bad blocks, writes nothing */
    NAND_ERASE,     /* does not read src */
};

struct nand_write_result {
    uint32_t done;      /* pages before the one that failed */
    uint32_t status;    /* chip status byte of the last erase or program */
    uint32_t bad[2];    /* one bit per block of the run */
};

/* Erases whole blocks and programs them from src, NAND_PAGE_RAW bytes per
 * page. page is the first page of a block, count covers at most 64 blocks,
 * and pages_per_block is a power of two.
 *
 * A bad block is not erased and not programmed, because an erase would remove
 * its marker. Block 0 is never bad. A page of 0xFF is not programmed. */
int nand_write_blocks(uint32_t page, uint32_t count, uint32_t pages_per_block,
                      enum nand_write_mode mode, const void *src,
                      struct nand_write_result *res);

#define IPL_START_BLOCK 2u

uint32_t nand_corrections(void);
uint32_t nand_retries(void);
