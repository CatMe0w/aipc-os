/*
 * The three boot paths.
 *
 * Each path loads with the MMU and the caches on. Then mmu_cache_disable()
 * cleans the caches and turns them off, because every payload here expects a
 * flat machine with no caches.
 */

#include "boot.h"
#include "eboot_patch.h"
#include "fat.h"
#include "lcd.h"
#include "log.h"
#include "mmu.h"
#include "nand.h"
#include "sd.h"
#include "soc.h"
#include "timer.h"
#include "layout.h"

/* Our own load address. No payload may grow into it. */
#define IMAGE_BASE   0x33000000u

#define ZIMAGE_NAME  "ZIMAGE     "
#define ZIMAGE_DST   0x30008000u
#define ZIMAGE_MAX   (IMAGE_BASE - ZIMAGE_DST)

/* gdbstub.bin is the build that links above our stack. The BOOT.BIN build of
 * gdbstub links at our own load address. */
#define GDBSTUB_NAME "GDBSTUB BIN"
#define GDBSTUB_DST  0x33A00000u
#define GDBSTUB_MAX  (FB_ADDR - GDBSTUB_DST)

#define EBOOT_DST    0x30037FD4u
#define EBOOT_BYTES  0x00064000u
/* The container header is 0x2C bytes, thus the first instruction is here. */
#define EBOOT_CODE   (EBOOT_DST + 0x2Cu)

extern const uint8_t nkpf_start[], nkpf_end[];

extern void jump_payload(uint32_t entry) __attribute__((noreturn));

static void quiesce(void)
{
    timer_stop();
    mmu_cache_disable();
}

static int load_file(const char *name11, uint32_t dst, uint32_t max,
                     uint32_t *size)
{
    int rc = fat_load_file(name11, (void *)(uintptr_t)dst, max, size);

    log_puts(name11);
    if (rc) {
        log_rc(rc);
        return rc;
    }
    log_puts(": ");
    log_dec(*size);
    log_puts(" bytes\n");
    return 0;
}

int boot_linux(void)
{
    uint32_t size = 0;
    int rc;

    /* A kernel is much larger than the gap to the log window. Anything logged
     * after this point would land inside the image that we load next. */
    log_detach();

    rc = load_file(ZIMAGE_NAME, ZIMAGE_DST, ZIMAGE_MAX, &size);
    if (rc)
        return rc;

    log_puts("linux: entering\n");
    quiesce();
    jump_payload(ZIMAGE_DST);
}

int boot_gdbstub(void)
{
    uint32_t size = 0;
    int rc = load_file(GDBSTUB_NAME, GDBSTUB_DST, GDBSTUB_MAX, &size);

    if (rc)
        return rc;

    log_puts("gdbstub: entering\n");
    quiesce();
    jump_payload(GDBSTUB_DST);
}

static void copy_blob(uint32_t dst, const uint8_t *start, const uint8_t *end)
{
    uint8_t *d = (uint8_t *)(uintptr_t)dst;

    while (start < end)
        *d++ = *start++;
}

/* Returns the nkpf address for the EBOOT handoff, or 0 to leave the handoff
 * alone. */
static uint32_t nkpf_install(void)
{
    uint32_t bytes = nkpf_end - nkpf_start;

    if (bytes > NKPF_MAX) {
        log_puts("nkpf: image too large\n");
        return 0;
    }
    copy_blob(NKPF_PHYS, nkpf_start, nkpf_end);
    return NKPF_PHYS;
}

int boot_wince(void)
{
    int rc;

    /* NAND and SD share a pad group, so the card must release it first. */
    sd_release_pins();
    nf_hw_init();
    nand_init();

    rc = nand_probe_geometry();
    if (rc) {
        log_puts("nand: geometry");
        log_rc(rc);
        return rc;
    }

    rc = nand_load_image((void *)(uintptr_t)EBOOT_DST, IPL_START_BLOCK,
                         EBOOT_BYTES);
    if (rc) {
        log_puts("nand: eboot load");
        log_rc(rc);
        return rc;
    }

    eboot_patch((uint32_t *)EBOOT_CODE, EBOOT_BYTES - 0x2Cu, nkpf_install());

    log_puts("eboot: entering\n");
    quiesce();
    handoff_eboot();
}
