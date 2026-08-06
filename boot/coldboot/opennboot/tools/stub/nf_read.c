/* Read a run of pages into NF_BUF, NF_PAGE_RAW bytes per page.
 *
 * Non-destructive. The host pulls NF_BUF back over usbboot afterwards.
 */

#include "nf_common.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x30113000u)

#define CMD_ROW   0     /* first page to read */
#define CMD_COUNT 1
#define CMD_RC    2
#define CMD_FAIL  3     /* page within the run that failed */
#define CMD_ID    4     /* first four bytes of READ ID, reported for diagnosis */

void stub_main(void)
{
    uint32_t row = CMD[CMD_ROW];
    uint32_t count = CMD[CMD_COUNT];
    volatile uint8_t *buf = (volatile uint8_t *)(uintptr_t)NF_BUF;
    uint32_t id = 0;
    int rc;
    uint32_t p = 0;

    CMD[CMD_RC] = 0xDEADu;
    CMD[CMD_FAIL] = 0;
    CMD[CMD_ID] = 0;

    if (count == 0 || count > NF_MAX_PAGES) {
        CMD[CMD_RC] = NF_E_ARG;
        return;
    }

    nf_hw_init();

    rc = nf_read_id(&id);
    CMD[CMD_ID] = id;

    if (!rc) {
        for (p = 0; p < count; p++) {
            rc = nf_read_page(row + p, buf + p * NF_PAGE_RAW);
            if (rc)
                break;
        }
    }

    CMD[CMD_FAIL] = p;
    CMD[CMD_RC] = (uint32_t)rc;
}
