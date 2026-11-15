#include "stub.h"
#include "block0.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x30113000u)

enum {
    B0_READ,    /* block at CMD_ROW -> B0_CURRENT */
    B0_BUILD,   /* B0_CURRENT and CMD_LEN bytes at B0_PAYLOAD -> B0_IMAGE */
    B0_CHECK,   /* checks B0_IMAGE */
    B0_WRITE,   /* B0_IMAGE -> block at CMD_ROW */
};

#define CMD_MODE   0
#define CMD_ROW    1    /* first page of the block */
#define CMD_LEN    2
#define CMD_RC     3
#define CMD_ID     4    /* B0_READ */
#define CMD_RESULT 5    /* B0_WRITE: struct block0_result, six words. B0_READ
                         * sets the page word to the page that failed */

void stub_main(void)
{
    uint32_t row = CMD[CMD_ROW];
    uint8_t *current = (uint8_t *)(uintptr_t)B0_CURRENT;
    uint8_t *image = (uint8_t *)(uintptr_t)B0_IMAGE;
    struct block0_result res;
    uint32_t id = 0, page = 0;
    int rc;

    CMD[CMD_RC] = 0xDEADu;
    CMD[CMD_ID] = 0;
    for (int i = 0; i < 6; ++i)
        CMD[CMD_RESULT + i] = 0;

    switch (CMD[CMD_MODE]) {
    case B0_READ:
        nf_hw_init();
        rc = nand_read_id(&id);
        if (!rc)
            rc = nand_read_pages_raw(row, BLOCK0_PAGES, current, &page);
        CMD[CMD_ID] = id;
        CMD[CMD_RESULT + 2] = page;
        break;
    case B0_BUILD:
        rc = block0_build(current, (const void *)(uintptr_t)B0_PAYLOAD,
                          CMD[CMD_LEN], image);
        break;
    case B0_CHECK:
        rc = block0_check(image);
        break;
    case B0_WRITE:
        nf_hw_init();
        rc = block0_write(row, image, (uint8_t *)(uintptr_t)B0_READBACK, &res);
        CMD[CMD_RESULT + 0] = res.step;
        CMD[CMD_RESULT + 1] = res.status;
        CMD[CMD_RESULT + 2] = res.page;
        CMD[CMD_RESULT + 3] = res.column;
        CMD[CMD_RESULT + 4] = res.got;
        CMD[CMD_RESULT + 5] = res.want;
        break;
    default:
        rc = STUB_EARG;
        break;
    }

    CMD[CMD_RC] = (uint32_t)rc;
}
