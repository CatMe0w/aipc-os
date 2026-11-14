#include "stub.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x30113000u)

#define CMD_ROW   0
#define CMD_COUNT 1
#define CMD_RC    2
#define CMD_FAIL  3     /* relative to CMD_ROW */
#define CMD_ID    4

void stub_main(void)
{
    uint32_t row = CMD[CMD_ROW];
    uint32_t count = CMD[CMD_COUNT];
    uint32_t id = 0;
    int rc;
    uint32_t p = 0;

    CMD[CMD_RC] = 0xDEADu;
    CMD[CMD_FAIL] = 0;
    CMD[CMD_ID] = 0;

    if (count == 0 || count > NF_MAX_PAGES) {
        CMD[CMD_RC] = STUB_EARG;
        return;
    }

    nf_hw_init();

    rc = nand_read_id(&id);
    CMD[CMD_ID] = id;

    if (!rc)
        rc = nand_read_pages_raw(row, count, (void *)(uintptr_t)NF_BUF, &p);

    CMD[CMD_FAIL] = p;
    CMD[CMD_RC] = (uint32_t)rc;
}
