#include <string.h>
#include "log.h"
#include "nand.h"
#include "sd.h"
#include "soc.h"
#include "sysinfo.h"

/* The version string comes before this offset in the IPL partition. */
#define IPL_SCAN_BYTES   16384u
#define PAGE_BYTES       2048u
#define CHUNK_BYTES      512u
/* Header fields of block 0, see docs/bootrom/nand-boot.md */
#define HDR_SIGNATURE    0x04u
#define HDR_PAGE_COUNT   0x0Du
#define NBOOT_SCAN_PAGES 31u
#define NBOOT_MIN_PAGES  8u

static uint8_t buf[(NBOOT_SCAN_PAGES + 1u) * PAGE_BYTES];
static char fw_version[16];
static char nboot_name[32];
static enum nboot_kind nboot;

static const uint8_t *find(const uint8_t *hay, uint32_t n, const char *needle,
                           const uint8_t *from)
{
    uint32_t len = strlen(needle);

    for (const uint8_t *p = from; p + len <= hay + n; p++)
        if (!memcmp(p, needle, len))
            return p;
    return NULL;
}

/* EBOOT prints " Version:1.58.2\r\n" in its banner. " Hardware Version:"
 * comes first and is a different number. */
static void read_fw_version(void)
{
    const uint8_t *p = buf;
    int rc = nand_probe_geometry();

    if (!rc)
        rc = nand_load_image(buf, IPL_START_BLOCK, IPL_SCAN_BYTES);
    if (rc) {
        log_puts("sysinfo: ipl");
        log_rc(rc);
        return;
    }
    while ((p = find(buf, IPL_SCAN_BYTES, " Version:", p))) {
        uint32_t n = 0;

        if (p - buf >= 8 && !memcmp(p - 8, "Hardware", 8)) {
            p++;
            continue;
        }
        p += 9;
        while (p + n < buf + IPL_SCAN_BYTES && p[n] != '\r' &&
               p[n] >= 0x20 && p[n] < 0x7F && n < sizeof(fw_version) - 1)
            n++;
        memcpy(fw_version, p, n);
        fw_version[n] = 0;
        return;
    }
}

static int read_page_raw(uint32_t page, uint8_t *dst)
{
    for (uint32_t c = 0; c < PAGE_BYTES; c += CHUNK_BYTES) {
        int rc = nand_read_raw(page, c, dst + c, CHUNK_BYTES);

        if (rc) {
            log_puts("sysinfo: block 0 page ");
            log_dec(page);
            log_rc(rc);
            return rc;
        }
    }
    return 0;
}

static void log_bytes(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 4)
        log_hex32((uint32_t)p[i] | (uint32_t)p[i + 1] << 8 |
                  (uint32_t)p[i + 2] << 16 | (uint32_t)p[i + 3] << 24);
    log_putc('\n');
}

/* openNBOOT keeps the string "openNBOOT" once in its image, at the start of
 * its banner. */
static void read_nboot(void)
{
    const uint8_t *banner;
    uint32_t pages;
    int header;

    nboot = NBOOT_UNKNOWN;
    if (read_page_raw(0, buf))
        return;
    header = !memcmp(buf + HDR_SIGNATURE, "ANYKA382", 8);
    pages = header ? buf[HDR_PAGE_COUNT] : 0;
    if (pages < NBOOT_MIN_PAGES)
        pages = NBOOT_MIN_PAGES;
    if (pages > NBOOT_SCAN_PAGES)
        pages = NBOOT_SCAN_PAGES;
    for (uint32_t p = 1; p <= pages; p++)
        if (read_page_raw(p, buf + p * PAGE_BYTES))
            return;
    banner = find(buf + PAGE_BYTES, pages * PAGE_BYTES, "openNBOOT",
                  buf + PAGE_BYTES);
    if (banner) {
        const uint8_t *end = buf + (pages + 1) * PAGE_BYTES;
        uint32_t n = 0;

        while (banner + n < end && banner[n] >= 0x20 && banner[n] < 0x7F &&
               n < sizeof(nboot_name) - 1)
            n++;
        memcpy(nboot_name, banner, n);
        nboot_name[n] = 0;
        nboot = NBOOT_OPENNBOOT;
    } else if (header) {
        nboot = NBOOT_STOCK;
    }

    log_puts("sysinfo: block 0 ");
    log_bytes(buf, 16);
    log_puts("sysinfo: nboot ");
    log_dec(nboot);
    log_putc('\n');
}

void sysinfo_read(void)
{
    sd_release_pins();
    nf_hw_init();
    nand_init();
    /* It probes the geometry, which reads NAND as the WinCE path does. That
     * must come before the raw reads of block 0. */
    read_fw_version();
    read_nboot();
}

const char *sysinfo_fw_version(void)
{
    return fw_version[0] ? fw_version : NULL;
}

enum nboot_kind sysinfo_nboot(void)
{
    return nboot;
}

const char *sysinfo_nboot_name(void)
{
    switch (nboot) {
    case NBOOT_OPENNBOOT:
        return nboot_name;
    case NBOOT_STOCK:
        return "Stock";
    default:
        return "Unknown";
    }
}

const char aipc_boot_version[] = AIPC_BOOT_VERSION;
