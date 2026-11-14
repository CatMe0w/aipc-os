/* The host owns every safety check except the argument bounds below. */

#include "stub.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x30117000u)

#define CMD_ROW    0    /* the block of this page is erased first */
#define CMD_COUNT  1
#define CMD_RC     2
#define CMD_FAIL   3    /* relative to CMD_ROW. CMD_COUNT if the erase failed */
#define CMD_STATUS 4

void stub_main(void)
{
    uint32_t row = CMD[CMD_ROW];
    uint32_t count = CMD[CMD_COUNT];
    const uint8_t *buf = (const uint8_t *)(uintptr_t)NF_BUF;
    uint32_t status = 0;
    int rc;
    uint32_t p = 0;

    CMD[CMD_RC] = 0xDEADu;
    CMD[CMD_FAIL] = 0;
    CMD[CMD_STATUS] = 0;

    if (count > NF_MAX_PAGES) {
        CMD[CMD_RC] = STUB_EARG;
        return;
    }

    nf_hw_init();

    rc = nand_erase_block(row, &status);
    if (rc) {
        CMD[CMD_FAIL] = count;
        CMD[CMD_STATUS] = status;
        CMD[CMD_RC] = (uint32_t)rc;
        return;
    }

    for (p = 0; p < count; p++) {
        rc = nand_program_page(row + p, buf + p * NAND_PAGE_RAW, NAND_PAGE_DATA, &status);
        if (rc)
            break;
    }

    CMD[CMD_FAIL] = p;
    CMD[CMD_STATUS] = status;
    CMD[CMD_RC] = (uint32_t)rc;
}
