#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "log.h"
#include "mmu.h"
#include "sd.h"
#include "storage.h"

static FATFS fs;
static int card_ready;
/* The card rejected CMD23, so storage_read() reads single blocks. */
static int single_blocks;

/* sd_read_block() and sd_write_block() take aligned buffers only. */
static uint32_t bounce[128];

/* The cluster link map of the file that storage_read() reads: 2 words for
 * each run of contiguous clusters, and 2 more. */
static DWORD clmt[512];

/* A card that does not take high speed still works at 20.67 MHz. */
static int card_start(void)
{
    int rc = sd_init();

    if (rc)
        return rc;
    rc = sd_high_speed();
    if (rc) {
        log_puts("storage: high speed");
        log_rc(rc);
    }
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv)
        return STA_NOINIT;
    if (!card_ready)
        card_ready = card_start() == 0;
    return card_ready ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv || !card_ready) ? STA_NOINIT : 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !card_ready)
        return RES_NOTRDY;
    for (UINT i = 0; i < count; i++, buff += 512) {
        int aligned = ((uintptr_t)buff & 3u) == 0;

        if (sd_read_block(sector + i, aligned ? (void *)buff : bounce))
            return RES_ERROR;
        if (!aligned)
            memcpy(buff, bounce, 512);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !card_ready)
        return RES_NOTRDY;
    for (UINT i = 0; i < count; i++, buff += 512) {
        const void *src = buff;

        if ((uintptr_t)buff & 3u) {
            memcpy(bounce, buff, 512);
            src = bounce;
        }
        if (sd_write_block(sector + i, src))
            return RES_ERROR;
    }
    return RES_OK;
}

/* sd_write_block() returns after the card has programmed the block, so
 * there is nothing left to flush. */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)buff;
    if (pdrv || !card_ready)
        return RES_NOTRDY;
    return cmd == CTRL_SYNC ? RES_OK : RES_PARERR;
}

int storage_mount(void)
{
    FRESULT fr;

    card_ready = 0;
    single_blocks = 0;
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        log_puts("storage: mount failed\n");
        return fr == FR_NOT_READY ? STORAGE_EIO : -(int)fr;
    }
    return 0;
}

int storage_space(uint32_t *free_mb, uint32_t *total_mb, const char **type)
{
    static const char *const types[] = { "FAT12", "FAT16", "FAT32", "exFAT" };
    FATFS *f;
    DWORD clusters;

    if (f_getfree("", &clusters, &f) != FR_OK)
        return STORAGE_EIO;
    /* 2048 sectors of 512 bytes make 1 MB. */
    *free_mb = (uint32_t)(((uint64_t)clusters * f->csize) >> 11);
    *total_mb = (uint32_t)(((uint64_t)(f->n_fatent - 2u) * f->csize) >> 11);
    *type = f->fs_type >= 1 && f->fs_type <= 4 ? types[f->fs_type - 1] : "FAT";
    return 0;
}

static int new_name(const char *path, char *out, size_t cap)
{
    size_t n = strlen(path);

    if (n + 5 > cap)
        return -1;
    memcpy(out, path, n);
    memcpy(out + n, ".new", 5);
    return 0;
}

static int read_slow(FIL *f, uint8_t *dst, UINT len)
{
    UINT got;

    if (f_read(f, dst, len, &got) != FR_OK || got != len)
        return STORAGE_EIO;
    return 0;
}

/* The sector at file position pos, and in *count the sectors from there to
 * the end of its run of contiguous clusters. */
static LBA_t run_at(FSIZE_t pos, UINT *count)
{
    DWORD cl = (DWORD)(pos / 512u / fs.csize);
    UINT in = (UINT)(pos / 512u % fs.csize);

    for (const DWORD *t = clmt + 1; t[0]; t += 2) {
        if (cl < t[0]) {
            *count = (t[0] - cl) * fs.csize - in;
            return fs.database + (t[1] + cl - 2u) * fs.csize + in;
        }
        cl -= t[0];
    }
    *count = 0;
    return 0;
}

int storage_read(FIL *f, void *dst, uint32_t len)
{
    uint8_t *d = dst;
    UINT head;
    int rc;

    if (!f->cltbl) {
        clmt[0] = sizeof(clmt) / sizeof(clmt[0]);
        f->cltbl = clmt;
        if (f_lseek(f, CREATE_LINKMAP) != FR_OK)
            f->cltbl = NULL;
    }
    head = (UINT)((512u - f_tell(f) % 512u) % 512u);
    if (head > len || !f->cltbl || single_blocks ||
        ((uintptr_t)(d + head) & 63u))
        head = len;
    if (read_slow(f, d, head))
        return STORAGE_EIO;
    d += head;
    len -= head;

    while (len >= 512u) {
        UINT n;
        LBA_t lba = run_at(f_tell(f), &n);

        if (n > len / 512u)
            n = len / 512u;
        if (n > SD_BLOCKS_MAX)
            n = SD_BLOCKS_MAX;
        if (n == 0)
            return STORAGE_EIO;
        dcache_flush();
        rc = sd_read_blocks(lba, n, d);
        if (rc == SD_ENOCMD23) {
            log_puts("storage: no CMD23, single blocks only\n");
            single_blocks = 1;
            break;
        }
        if (rc) {
            log_puts("storage: multiple block read failed\n");
            if (card_start())
                return STORAGE_EIO;
            break;
        }
        if (f_lseek(f, f_tell(f) + n * 512u) != FR_OK)
            return STORAGE_EIO;
        d += n * 512u;
        len -= n * 512u;
    }
    return read_slow(f, d, len);
}

static int load_one(const char *path, void *dst, uint32_t max,
                    uint32_t *size)
{
    FIL f;
    FSIZE_t want;
    int rc;
    FRESULT fr = f_open(&f, path, FA_READ);

    if (fr == FR_NO_FILE || fr == FR_NO_PATH)
        return STORAGE_ENOTFOUND;
    if (fr != FR_OK)
        return STORAGE_EIO;
    want = f_size(&f);
    if (want == 0 || want > max) {
        f_close(&f);
        return STORAGE_ESIZE;
    }
    rc = storage_read(&f, dst, (uint32_t)want);
    f_close(&f);
    if (rc)
        return rc;
    *size = (uint32_t)want;
    return 0;
}

int storage_load(const char *path, void *dst, uint32_t max, uint32_t *size)
{
    static char tmp[FF_MAX_LFN + 1];
    int rc = load_one(path, dst, max, size);

    if (rc != STORAGE_ENOTFOUND || new_name(path, tmp, sizeof(tmp)))
        return rc;
    return load_one(tmp, dst, max, size);
}

int storage_save(const char *path, const void *src, uint32_t len)
{
    static char tmp[FF_MAX_LFN + 1];
    FIL f;
    UINT put;
    FRESULT fr;

    if (new_name(path, tmp, sizeof(tmp)))
        return STORAGE_EIO;

    fr = f_open(&f, tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK)
        return STORAGE_EIO;
    fr = f_write(&f, src, len, &put);
    if (f_close(&f) != FR_OK || fr != FR_OK || put != len) {
        f_unlink(tmp);
        return STORAGE_EIO;
    }
    fr = f_unlink(path);
    if (fr != FR_OK && fr != FR_NO_FILE)
        return STORAGE_EIO;
    return f_rename(tmp, path) == FR_OK ? 0 : STORAGE_EIO;
}
