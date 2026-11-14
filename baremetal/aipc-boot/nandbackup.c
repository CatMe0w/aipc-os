#include <string.h>
#include "ff.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/lz4/lz4.h"
#include "log.h"
#include "nand.h"
#include "soc.h"
#include "nandbackup.h"
#include "storage.h"
#include "xxh32.h"

#define DIR      "/backup"
#define PARTIAL  DIR "/backup_nand.img.partial"

/* One batch is one LZ4 block, so it must fit the 4 MB block size that the
 * frame header declares. progress() runs once for each batch. */
#define BATCH_PAGES  1024u
#define BATCH_BYTES  (BATCH_PAGES * NAND_PAGE_RAW)
#define BATCH_BUF    ((uint8_t *)(uintptr_t)0x31000000u)

/* The compressed data waits here for the next time the card has the pads. */
#define OUT_BUF      ((uint8_t *)(uintptr_t)0x31400000u)
#define OUT_FLUSH    0x00800000u

#define LZ4_MAGIC         0x184D2204u
#define LZ4_FLG           0x64u     /* version 1, independent blocks, content checksum */
#define LZ4_BD            0x70u     /* 4 MB blocks */
#define LZ4_UNCOMPRESSED  0x80000000u

static uint32_t used;

static void put32(uint32_t v)
{
    memcpy(OUT_BUF + used, &v, 4);
    used += 4;
}

int nandtool_card_select(void)
{
    int rc = storage_resume();

    if (rc)
        log_puts("nandbackup: the card did not start again\n");
    return rc;
}

void nandtool_nand_select(void)
{
    storage_suspend();
    nf_hw_init();
    nand_init();
}

/* Writes whole sectors only, from the start of OUT_BUF. FatFs then passes the
 * buffer to the card as it is, with the alignment that the DMA needs. */
static int flush(FIL *f, int all)
{
    uint32_t n = all ? used : used & ~511u;
    UINT put = 0;
    FRESULT fr = f_write(f, OUT_BUF, n, &put);

    if (fr != FR_OK || put != n) {
        log_puts("nandbackup: wrote ");
        log_dec(put);
        log_puts(" of ");
        log_dec(n);
        log_puts(" bytes, FatFs code ");
        log_dec((uint32_t)fr);
        log_putc('\n');
        return -1;
    }
    memmove(OUT_BUF, OUT_BUF + n, used - n);
    used -= n;
    return 0;
}

static int copy(FIL *f, uint32_t pages, uint32_t *checksum,
                int (*progress)(uint32_t))
{
    struct xxh32 hash;
    uint8_t header[2] = { LZ4_FLG, LZ4_BD };

    xxh32_init(&hash);
    xxh32_update(&hash, header, 2);
    used = 0;
    put32(LZ4_MAGIC);
    OUT_BUF[used++] = LZ4_FLG;
    OUT_BUF[used++] = LZ4_BD;
    OUT_BUF[used++] = (uint8_t)(xxh32_digest(&hash) >> 8);

    xxh32_init(&hash);
    nandtool_nand_select();
    for (uint32_t page = 0; page < pages; page += BATCH_PAGES) {
        uint32_t count = pages - page < BATCH_PAGES ? pages - page : BATCH_PAGES;
        uint32_t bytes = count * NAND_PAGE_RAW, done;
        int rc = nand_read_pages_raw(page, count, BATCH_BUF, &done);
        int n;

        if (rc) {
            log_puts("nandbackup: read failed at page ");
            log_dec(page + done);
            log_rc(rc);
            return NANDBACKUP_ENAND;
        }
        xxh32_update(&hash, BATCH_BUF, bytes);

        n = LZ4_compress_default((const char *)BATCH_BUF,
                                 (char *)OUT_BUF + used + 4, (int)bytes,
                                 (int)bytes - 1);
        if (n > 0) {
            put32((uint32_t)n);
            used += (uint32_t)n;
        } else {
            put32(bytes | LZ4_UNCOMPRESSED);
            memcpy(OUT_BUF + used, BATCH_BUF, bytes);
            used += bytes;
        }

        if (progress(1000u * (page + count) / pages))
            return NANDBACKUP_CANCELLED;
        if (used >= OUT_FLUSH) {
            if (nandtool_card_select() || flush(f, 0))
                return NANDBACKUP_ECARD;
            nandtool_nand_select();
        }
    }

    *checksum = xxh32_digest(&hash);
    put32(0);
    put32(*checksum);
    if (nandtool_card_select() || flush(f, 1))
        return NANDBACKUP_ECARD;
    return NANDBACKUP_OK;
}

int nand_backup(int (*progress)(uint32_t permille), char *name, uint32_t size)
{
    static char path[80];
    uint32_t id, pages, ppb, checksum = 0;
    FIL f;
    FRESULT fr;
    int rc;

    nandtool_nand_select();
    rc = nand_read_id(&id);
    if (rc == 0)
        rc = nand_id_geometry(id, &pages, &ppb);
    if (nandtool_card_select())
        return NANDBACKUP_ECARD;
    if (rc) {
        log_puts("nandbackup: NAND ID");
        log_rc(rc);
        return NANDBACKUP_ENAND;
    }

    fr = f_mkdir(DIR);
    if (fr != FR_OK && fr != FR_EXIST)
        return NANDBACKUP_ECARD;
    if (f_open(&f, PARTIAL, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return NANDBACKUP_ECARD;

    rc = copy(&f, pages, &checksum, progress);
    /* copy() can stop with the pads on the NAND side. */
    if (rc != NANDBACKUP_OK && rc != NANDBACKUP_ECARD && nandtool_card_select())
        rc = NANDBACKUP_ECARD;
    if (f_close(&f) != FR_OK && rc == NANDBACKUP_OK)
        rc = NANDBACKUP_ECARD;
    if (rc != NANDBACKUP_OK) {
        f_unlink(PARTIAL);
        return rc;
    }

    /* The ID bytes in the order that READ ID gives them, as the host tool
     * prints them. */
    lv_snprintf(name, size, "backup_nand-%02x%02x%02x%02x-%08x.img.lz4",
                (unsigned)(id & 0xFFu), (unsigned)((id >> 8) & 0xFFu),
                (unsigned)((id >> 16) & 0xFFu), (unsigned)(id >> 24),
                (unsigned)checksum);
    lv_snprintf(path, sizeof(path), DIR "/%s", name);
    fr = f_rename(PARTIAL, path);
    if (fr == FR_EXIST) {
        f_unlink(PARTIAL);
        return NANDBACKUP_EEXISTS;
    }
    return fr == FR_OK ? NANDBACKUP_OK : NANDBACKUP_ECARD;
}
