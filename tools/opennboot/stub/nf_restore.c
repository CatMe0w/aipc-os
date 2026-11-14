/* The host owns every safety check except the argument bounds below. */

#include "stub.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x3011F000u)

/* The first four words are the arguments of nand_write_blocks(). */
#define CMD_ROW    0
#define CMD_COUNT  1
#define CMD_PPB    2
#define CMD_MODE   3    /* enum nand_write_mode */
#define CMD_RC     4
#define CMD_FAIL   5    /* relative to CMD_ROW */
#define CMD_STATUS 6
#define CMD_BAD    7    /* two words, one bit per block of the run */

void stub_main(void)
{
    uint32_t row = CMD[CMD_ROW];
    uint32_t count = CMD[CMD_COUNT];
    struct nand_write_result res;
    int rc;

    CMD[CMD_RC] = 0xDEADu;

    if (count > NF_BACKUP_MAX_PAGES) {
        CMD[CMD_RC] = STUB_EARG;
        return;
    }

    nf_hw_init();

    rc = nand_write_blocks(row, count, CMD[CMD_PPB],
                           (enum nand_write_mode)CMD[CMD_MODE],
                           (const void *)(uintptr_t)NF_BACKUP_BUF, &res);

    CMD[CMD_BAD] = res.bad[0];
    CMD[CMD_BAD + 1] = res.bad[1];
    CMD[CMD_FAIL] = res.done;
    CMD[CMD_STATUS] = res.status;
    CMD[CMD_RC] = (uint32_t)rc;
}
