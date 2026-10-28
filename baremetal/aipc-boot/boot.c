#include <string.h>
#include "boot.h"
#include "cfw_abi.h"
#include "entries.h"
#include "log.h"
#include "mmu.h"
#include "nand.h"
#include "sd.h"
#include "settings.h"
#include "soc.h"
#include "storage.h"
#include "timer.h"

/* Our own load address. No payload may grow into it. */
#define IMAGE_BASE   0x33000000u

#define ZIMAGE_DST   0x30008000u
#define ZIMAGE_MAX   (IMAGE_BASE - ZIMAGE_DST)
/* The kernel decompressor merges these tags into the appended DTB. */
#define ATAGS_ADDR   0x30000100u

#define ATAG_CORE    0x54410001u
#define ATAG_CMDLINE 0x54410009u

#define EBOOT_DST    0x30037FD4u
#define EBOOT_BYTES  0x00064000u
/* The container header is 0x2C bytes, thus the first instruction is here. */
#define EBOOT_CODE   (EBOOT_DST + 0x2Cu)

extern const uint8_t cfw_start[], cfw_end[];

extern void jump_payload(uint32_t entry) __attribute__((noreturn));
extern void call_payload(uint32_t entry);
extern void jump_linux(uint32_t entry, uint32_t atags)
    __attribute__((noreturn));

/* Every entry below expects the MMU and the caches off. */
static void quiesce(void)
{
    timer_stop();
    mmu_cache_disable();
}

void boot_jump(uint32_t entry)
{
    log_puts("payload: entering\n");
    quiesce();
    jump_payload(entry);
}

void payload_call(uint32_t entry)
{
    log_puts("payload: calling\n");
    quiesce();
    call_payload(entry);
    boot_hw_init();
    log_puts("payload: returned\n");
}

/* ATAG_CORE, ATAG_CMDLINE, and the terminator. */
static uint32_t build_atags(const char *cmdline)
{
    uint32_t *t = (uint32_t *)(uintptr_t)ATAGS_ADDR;
    uint32_t len = strlen(cmdline) + 1;

    *t++ = 2;
    *t++ = ATAG_CORE;
    *t++ = 2 + (len + 3) / 4;
    *t++ = ATAG_CMDLINE;
    memset(t, 0, (len + 3) & ~3u);
    memcpy(t, cmdline, len);
    t += (len + 3) / 4;
    *t++ = 0;
    *t = 0;
    return ATAGS_ADDR;
}

int linux_load(const struct entry *e)
{
    uint32_t size = 0;

    /* The kernel covers the log window. See README.md#log. */
    log_detach();

    return storage_load(e->kernel, (void *)(uintptr_t)ZIMAGE_DST, ZIMAGE_MAX,
                        &size);
}

void linux_enter(const struct entry *e)
{
    log_puts("linux: entering\n");
    if (!e->cmdline[0]) {
        quiesce();
        jump_payload(ZIMAGE_DST);
    }
    uint32_t atags = build_atags(e->cmdline);

    quiesce();
    jump_linux(ZIMAGE_DST, atags);
}

/* Copies CFW to CFW_PHYS, where it is linked to run. The CFW link checks
 * that the image fits below CFW_PHYS + CFW_MAX. */
static const struct cfw_header *cfw_install(void)
{
    memcpy((void *)CFW_PHYS, cfw_start, (size_t)(cfw_end - cfw_start));
    cache_sync();
    return (const struct cfw_header *)CFW_PHYS;
}

/* The image in .rodata. Its pointers hold CFW_PHYS addresses. */
static const void *cfw_blob_ptr(const void *p)
{
    return cfw_start + ((uintptr_t)p - CFW_PHYS);
}

int cfw_component_count(void)
{
    return ((const struct cfw_header *)cfw_start)->component_count;
}

const struct cfw_component_info *cfw_component(int i)
{
    const struct cfw_header *hdr = (const struct cfw_header *)cfw_start;
    const struct cfw_component_info *const *list =
        cfw_blob_ptr(hdr->components);

    return cfw_blob_ptr(list[i]);
}

int cfw_component_on(const struct cfw_component_info *c)
{
    int on = settings_cfw(c->id);

    return on < 0 ? (c->flags & CFW_DEFAULT_ON) != 0 : on;
}

/* The saved state of each component, or its default. */
static uint32_t cfw_mask(void)
{
    uint32_t mask = 0;

    for (int i = 0; i < cfw_component_count(); i++)
        if (cfw_component_on(cfw_component(i)))
            mask |= 1u << i;
    return mask;
}

static char cfw_message[sizeof(((struct cfw_result *)0)->message)];

const char *cfw_error(void)
{
    return cfw_message;
}

int wince_power_held(int cfw)
{
    for (int i = 0; cfw && i < cfw_component_count(); i++)
        if (!strcmp(cfw_component(i)->id, "power_hold"))
            return cfw_component_on(cfw_component(i));
    return 0;
}

/* Patches the loaded EBOOT. Returns 0, or BOOT_ECFW. */
static int cfw_eboot_stage(void)
{
    const struct cfw_header *hdr = cfw_install();
    struct cfw_eboot_args args;
    struct cfw_result result;

    args.code = EBOOT_CODE;
    args.bytes = EBOOT_BYTES - 0x2Cu;
    args.enable = cfw_mask();
    if (hdr->eboot_stage(&args, &result)) {
        log_puts("cfw: ");
        log_puts(result.message);
        log_putc('\n');
        memcpy(cfw_message, result.message, sizeof(cfw_message));
        cfw_message[sizeof(cfw_message) - 1] = 0;
        return BOOT_ECFW;
    }
    return 0;
}

int wince_prepare(int cfw)
{
    int rc;

    /* NAND and SD share a pad group, so SD must release it first. */
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

    return cfw ? cfw_eboot_stage() : 0;
}

void wince_enter(void)
{
    log_puts("eboot: entering\n");
    quiesce();
    handoff_eboot();
}
