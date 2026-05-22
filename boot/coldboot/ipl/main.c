#include <stdint.h>
#include "log.h"
#include "nand.h"
#include "sd.h"
#include "fat.h"

/* BAK partition coordinates for v1.58.2 (block_size 128KB; IPL.start=2,
 * IPL.count=16; so BAK.start=18). */
#define BAK_START_BLOCK   18u

/* nboot loads IPL to 0x30037FD4 (IMG header at -0x2C, payload at 0x30038000)
 * and reads exactly 0x64000 bytes. We replicate that contract for BAK. */
#define HANDOFF_DST       0x30037FD4u
#define HANDOFF_ENTRY     0x30038000u
#define HANDOFF_BYTES     0x64000u

/* SD-side handoff: BOOT.BIN lands at 0x33000000 (DDR top region, far from
 * IPL@0x32000000 and BAK handoff@0x30038000). 12 MB cap leaves a 4 MB
 * safety margin before the 64 MB DDR end at 0x34000000. Big enough for a
 * zImage with embedded initramfs. */
#define SD_BOOT_DST       0x33000000u
#define SD_BOOT_MAX       0x00A00000u  /* 10 MB */

static void log_hex_bytes(const uint8_t *p, uint32_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < n; ++i) {
        log_putc(hex[(p[i] >> 4) & 0xFu]);
        log_putc(hex[p[i] & 0xFu]);
        log_putc(' ');
    }
    log_putc('\n');
}

/* Try to load BOOT.BIN from the first FAT partition of the SD card into
 * SD_BOOT_DST. Returns 0 on success (caller jumps), negative on any
 * failure (no card, no FAT partition, file missing, ECC, etc — caller
 * falls back to NAND BAK in any of those cases). Always restores
 * sharepin to NAND mode before returning. */
static int sd_try_boot(uint32_t *out_size)
{
    int rc = sd_init();
    log_puts("sd_init rc=");
    log_put_hex32((uint32_t)rc);
    log_putc('\n');
    if (rc != 0) {
        sd_release_pins();
        return rc;
    }

    uint32_t size = 0;
    rc = fat_load_file("BOOT    BIN", (void *)(uintptr_t)SD_BOOT_DST,
                       SD_BOOT_MAX, &size);
    log_puts("fat_load_file rc=");
    log_put_hex32((uint32_t)rc);
    log_putc('\n');
    sd_release_pins();
    if (rc != 0)
        return rc;

    log_puts("BOOT.BIN size=");
    log_put_hex32(size);
    log_putc('\n');

    if (out_size)
        *out_size = size;
    return 0;
}

void ipl_main(void)
{
    log_init();
    log_puts("IPL alive at 0x32000000\n");

    uint32_t sd_size = 0;
    int sd_rc = sd_try_boot(&sd_size);
    (void)sd_size;
    if (sd_rc == 0) {
        log_puts("jumping to BOOT.BIN @0x33000000\n");
        __asm__ volatile (
            "ldr pc, =0x33000000\n"
            ::: "memory"
        );
        __builtin_unreachable();
    }

    /* SD path unavailable (no card, no BOOT.BIN, read error) fall back
     * to the NAND BAK partition so WinCE still boots. */

    /* Load BAK directly to the handoff destination. We're running from
     * 0x32000000 now so writing to 0x30037FD4 doesn't touch our code. */
    int rc = nand_load_partition((void *)(uintptr_t)HANDOFF_DST,
                                  BAK_START_BLOCK, HANDOFF_BYTES);
    log_puts("BAK load rc=");
    log_put_hex32((uint32_t)rc);
    log_putc('\n');

    if (rc != 0) {
        log_puts("BAK load failed; halting\n");
        for (;;)
            ;
    }

    log_puts("handoff[0:16]=");
    log_hex_bytes((const uint8_t *)(uintptr_t)HANDOFF_DST, 16);
    log_puts("handoff[0x2C:0x10]=");
    log_hex_bytes((const uint8_t *)(uintptr_t)(HANDOFF_DST + 0x2C), 16);

    log_puts("jumping to eboot @0x30038000\n");

    /* Hand off in SVC mode with the SP nboot would have left for eboot
     * (matches the implicit contract at docs/nboot/boot-flow.md:81). */
    __asm__ volatile (
        "ldr sp, =0x30036000\n"
        "ldr pc, =0x30038000\n"
        ::: "memory"
    );

    __builtin_unreachable();
}
