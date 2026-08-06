#include "fat.h"
#include "log.h"
#include "nand.h"
#include "sd.h"
#include "soc.h"

#define HANDOFF_DST     0x30037FD4u  /* IMG header lands here; entry at +0x2C */
#define HANDOFF_BYTES   0x00064000u

#define SD_BOOT_DST     0x33000000u
#define SD_BOOT_MAX     0x00A00000u  /* 10 MB */

static uint32_t sum32(const uint8_t *p, uint32_t len)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < len; ++i)
        s = (s << 1) + (s >> 31) + p[i];
    return s;
}

static __attribute__((noreturn)) void halt(const char *why, int rc)
{
    log_puts("halt: ");
    log_puts(why);
    log_rc(rc);
    for (;;)
        ;
}

static int sd_try_boot(uint32_t *out_size)
{
    int rc = sd_init();
    if (rc) {
        log_puts("sd: no card");
        log_rc(rc);
        sd_release_pins();
        return rc;
    }

    rc = fat_load_file("BOOT    BIN", (void *)(uintptr_t)SD_BOOT_DST,
                       SD_BOOT_MAX, out_size);
    sd_release_pins();
    if (rc) {
        log_puts("sd: no BOOT.BIN");
        log_rc(rc);
        return rc;
    }
    return 0;
}

void boot_main(void)
{
    uart_init();
    log_init();
    log_puts("openNBOOT\n");

    l2_init();

    uint32_t size = 0;
    if (sd_try_boot(&size) == 0) {
        log_puts("sd: BOOT.BIN ");
        log_dec(size);
        log_puts(" bytes, entering\n");
        handoff_bare(SD_BOOT_DST);
    }

    nf_hw_init();
    nand_init();

    int rc = nand_probe_geometry();
    if (rc)
        halt("geometry probe", rc);

    rc = nand_load_image((void *)(uintptr_t)HANDOFF_DST,
                             IPL_START_BLOCK, HANDOFF_BYTES);
    if (rc)
        halt("ipl load", rc);

    log_puts("ipl loaded, sum=");
    log_hex32(sum32((const uint8_t *)(uintptr_t)HANDOFF_DST, HANDOFF_BYTES));
    log_puts(" corrections=");
    log_dec(nand_corrections());
    log_puts(" retries=");
    log_dec(nand_retries());
    log_puts("\neboot: entering\n");

    handoff_eboot();
}
