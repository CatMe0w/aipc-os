#include <string.h>
#include "block0.h"
#include "ff.h"
#include "lvgl/lvgl.h"
#include "log.h"
#include "nand.h"
#include "nandbackup.h"
#include "nboottool.h"
#include "storage.h"
#include "sysinfo.h"
#include "xxh32.h"

#define BACKUP  "/backup"
#define PREFIX  "backup_nboot-"

/* One block 0 image each. The NAND backup uses the same DDR, so nothing here
 * stays valid across a backup or a restore. */
#define CURRENT   ((uint8_t *)(uintptr_t)0x31000000u)   /* block 0 as read */
#define IMAGE     ((uint8_t *)(uintptr_t)0x31040000u)   /* the image to write */
#define READBACK  ((uint8_t *)(uintptr_t)0x31080000u)
#define FILE_BUF  ((uint8_t *)(uintptr_t)0x310C0000u)

#define HDR_PAGE_COUNT  0x0Du

extern const uint8_t opennboot_start[], opennboot_end[];

static uint32_t nand_id;

const char *nboot_bundled_name(void)
{
    static const char mark[] = "openNBOOT";
    static char name[32];
    const uint8_t *p = opennboot_start;
    uint32_t n = 0;

    if (name[0])
        return name;
    while (p + sizeof(mark) <= opennboot_end && memcmp(p, mark, sizeof(mark) - 1))
        p++;
    while (p + n < opennboot_end && p[n] >= 0x20 && p[n] < 0x7F &&
           n < sizeof(name) - 1)
        n++;
    memcpy(name, p, n);
    return name;
}

static int read_current(void)
{
    uint32_t done = 0;
    int rc;

    nandtool_nand_select();
    rc = nand_read_id(&nand_id);
    if (rc == 0)
        rc = nand_read_pages_raw(0, BLOCK0_PAGES, CURRENT, &done);
    if (nandtool_card_select())
        return NBOOTTOOL_ECARD;
    if (rc) {
        log_puts("nboottool: read failed at page ");
        log_dec(done);
        log_rc(rc);
        return NBOOTTOOL_ENAND;
    }
    return NBOOTTOOL_OK;
}

static void id_text(char *out)
{
    lv_snprintf(out, 9, "%02x%02x%02x%02x", (unsigned)(nand_id & 0xFFu),
                (unsigned)((nand_id >> 8) & 0xFFu),
                (unsigned)((nand_id >> 16) & 0xFFu), (unsigned)(nand_id >> 24));
}

int nboot_prepare_install(void)
{
    int rc = read_current();

    if (rc)
        return rc;
    rc = block0_build(CURRENT, opennboot_start,
                      (uint32_t)(opennboot_end - opennboot_start), IMAGE);
    if (rc) {
        log_puts("nboottool: build");
        log_rc(rc);
        return NBOOTTOOL_EBLOCK0;
    }
    return NBOOTTOOL_OK;
}

static int is_hex(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return 1;
}

static int has_id(const char *name)
{
    const char *p = name + sizeof(PREFIX) - 1;

    return strlen(p) == 8 + 1 + 8 + 4 && is_hex(p, 8) && p[8] == '-' &&
           is_hex(p + 9, 8);
}

static int other_device(const char *name)
{
    char id[9];

    id_text(id);
    return memcmp(name + sizeof(PREFIX) - 1, id, 8) != 0;
}

static int damaged(const char *name, const uint8_t *image)
{
    struct xxh32 hash;
    char sum[9];

    xxh32_init(&hash);
    xxh32_update(&hash, image, BLOCK0_BYTES);
    lv_snprintf(sum, sizeof(sum), "%08x", (unsigned)xxh32_digest(&hash));
    return memcmp(name + sizeof(PREFIX) - 1 + 9, sum, 8) != 0;
}

static int same_header(const uint8_t *a, const uint8_t *b)
{
    return !memcmp(a, b, HDR_PAGE_COUNT) &&
           !memcmp(a + HDR_PAGE_COUNT + 1u, b + HDR_PAGE_COUNT + 1u,
                   NAND_PAGE_DATA - HDR_PAGE_COUNT - 1u);
}

int nboot_prepare_remove(char *name, uint32_t size)
{
    static char path[80];
    DIR dir;
    FILINFO info;
    int found = 0, bad = 0;
    int rc = read_current();

    if (rc)
        return rc;
    if (block0_check(CURRENT))
        return NBOOTTOOL_EBLOCK0;

    if (f_opendir(&dir, BACKUP) != FR_OK)
        return NBOOTTOOL_ENOBACKUP;
    rc = NBOOTTOOL_OK;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        size_t n = strlen(info.fname);
        uint32_t got;

        if ((info.fattrib & AM_DIR) || info.fsize != BLOCK0_BYTES ||
            n < sizeof(PREFIX) + 3 || n >= size ||
            strncmp(info.fname, PREFIX, sizeof(PREFIX) - 1) ||
            strcmp(info.fname + n - 4, ".bin") ||
            (has_id(info.fname) && other_device(info.fname)))
            continue;
        lv_snprintf(path, sizeof(path), BACKUP "/%s", info.fname);
        if (storage_load(path, FILE_BUF, BLOCK0_BYTES, &got)) {
            rc = NBOOTTOOL_ECARD;
            break;
        }
        if (has_id(info.fname) && damaged(info.fname, FILE_BUF)) {
            log_puts("nboottool: damaged backup ");
            log_puts(info.fname);
            log_putc('\n');
            bad = 1;
            continue;
        }
        if (block0_check(FILE_BUF) || !same_header(FILE_BUF, CURRENT) ||
            sysinfo_image_kind(FILE_BUF) != NBOOT_STOCK)
            continue;
        if (found && memcmp(FILE_BUF, IMAGE, BLOCK0_BYTES)) {
            rc = NBOOTTOOL_EAMBIGUOUS;
            break;
        }
        if (!found) {
            memcpy(IMAGE, FILE_BUF, BLOCK0_BYTES);
            strcpy(name, info.fname);
        }
        found = 1;
    }
    f_closedir(&dir);
    if (rc)
        return rc;
    if (found)
        return NBOOTTOOL_OK;
    return bad ? NBOOTTOOL_EDAMAGED : NBOOTTOOL_ENOBACKUP;
}

static int saved(const char *path)
{
    uint32_t got;

    return f_stat(path, NULL) == FR_OK &&
           !storage_load(path, FILE_BUF, BLOCK0_BYTES, &got) &&
           got == BLOCK0_BYTES && !memcmp(FILE_BUF, CURRENT, BLOCK0_BYTES);
}

int nboot_backup(char *name, uint32_t size)
{
    static char path[80];
    struct xxh32 hash;
    char id[9];
    FRESULT fr;

    xxh32_init(&hash);
    xxh32_update(&hash, CURRENT, BLOCK0_BYTES);
    id_text(id);
    lv_snprintf(name, size, PREFIX "%s-%08x.bin", id,
                (unsigned)xxh32_digest(&hash));
    lv_snprintf(path, sizeof(path), BACKUP "/%s", name);

    fr = f_mkdir(BACKUP);
    if (fr != FR_OK && fr != FR_EXIST)
        return NBOOTTOOL_ECARD;
    /* The name holds the checksum. A file of that name with other contents
     * is a damaged backup, and this one replaces it. */
    if (saved(path))
        return NBOOTTOOL_OK;
    if (storage_save(path, CURRENT, BLOCK0_BYTES) || !saved(path)) {
        log_puts("nboottool: the backup does not read back\n");
        return NBOOTTOOL_ECARD;
    }
    return NBOOTTOOL_OK;
}

static void log_result(const char *what, int rc, const struct block0_result *res)
{
    log_puts("nboottool: ");
    log_puts(what);
    log_puts(" failed, step ");
    log_dec(res->step);
    log_puts(" page ");
    log_dec(res->page);
    log_puts(" column ");
    log_dec(res->column);
    log_puts(" status ");
    log_dec(res->status);
    log_rc(rc);
}

int nboot_write(void)
{
    struct block0_result res;
    int rc, erased = 0, back = 0;

    nandtool_nand_select();
    rc = block0_write(0, IMAGE, READBACK, &res);
    if (rc == 0) {
        sysinfo_set_block0(IMAGE);
    } else {
        log_result("write", rc, &res);
        erased = res.step != BLOCK0_CHECK;
        if (erased) {
            back = block0_write(0, CURRENT, READBACK, &res);
            if (back)
                log_result("rollback", back, &res);
        }
    }
    nandtool_card_select();

    if (rc == 0)
        return NBOOTTOOL_OK;
    if (!erased)
        return NBOOTTOOL_EBLOCK0;
    return back ? NBOOTTOOL_EROLLBACK : NBOOTTOOL_EWRITE;
}
