#include "stub.h"

#define CMD ((volatile uint32_t *)(uintptr_t)0x3011B000u)

#define CMD_ROW   0
#define CMD_COUNT 1
#define CMD_RC    2
#define CMD_FAIL  3     /* relative to CMD_ROW */
#define CMD_ID    4

/* The bootrom leaves the instruction cache off, which makes a read 2.4 times
 * slower. The cache keeps its contents while it is off. Without the
 * invalidation, code from an earlier upload at the same address runs. */
static uint32_t icache_enable(void)
{
    uint32_t old;

    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" : : "r"(0));
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(old));
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(old | 0x1000u));
    return old;
}

static void sctlr_restore(uint32_t old)
{
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(old));
}

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

    if (count == 0 || count > NF_BACKUP_MAX_PAGES) {
        CMD[CMD_RC] = STUB_EARG;
        return;
    }

    uint32_t sctlr = icache_enable();

    nf_hw_init();

    rc = nand_read_id(&id);
    CMD[CMD_ID] = id;

    if (!rc)
        rc = nand_read_pages_raw(row, count, (void *)(uintptr_t)NF_BACKUP_BUF, &p);

    sctlr_restore(sctlr);

    CMD[CMD_FAIL] = p;
    CMD[CMD_RC] = (uint32_t)rc;
}
