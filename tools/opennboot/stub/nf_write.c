/* Erase the block that holds a given page, then program a run of pages from NF_BUF.
 *
 * This stub does not decide what to write. The host owns every safety check
 * except the argument bounds below.
 */

#include "nf_common.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x30117000u)

#define CMD_ROW    0    /* first page to program. its block is the one erased */
#define CMD_COUNT  1
#define CMD_RC     2
#define CMD_FAIL   3    /* page within the run that failed. count on erase failure */
#define CMD_STATUS 4    /* raw chip status byte. the host decodes bit 7 for WP */

void stub_main(void)
{
    uint32_t row = CMD[CMD_ROW];
    uint32_t count = CMD[CMD_COUNT];
    const volatile uint8_t *buf = (const volatile uint8_t *)(uintptr_t)NF_BUF;
    uint32_t status = 0;
    int rc;
    uint32_t p = 0;

    CMD[CMD_RC] = 0xDEADu;
    CMD[CMD_FAIL] = 0;
    CMD[CMD_STATUS] = 0;

    if (count > NF_MAX_PAGES) {
        CMD[CMD_RC] = NF_E_ARG;
        return;
    }

    nf_hw_init();

    rc = nf_erase_block(row, &status);
    if (rc) {
        CMD[CMD_FAIL] = count;
        CMD[CMD_STATUS] = status;
        CMD[CMD_RC] = (uint32_t)rc;
        return;
    }

    for (p = 0; p < count; p++) {
        rc = nf_program_page(row + p, buf + p * NF_PAGE_RAW, &status);
        if (rc)
            break;
    }

    CMD[CMD_FAIL] = p;
    CMD[CMD_STATUS] = status;
    CMD[CMD_RC] = (uint32_t)rc;
}
