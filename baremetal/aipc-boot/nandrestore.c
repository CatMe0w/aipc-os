#include <string.h>
#include "ff.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/lz4/lz4.h"
#include "log.h"
#include "nand.h"
#include "nandbackup.h"
#include "storage.h"
#include "xxh32.h"

#define BATCH_PAGES  1024u
#define BATCH_BYTES  (BATCH_PAGES * NAND_PAGE_RAW)

/* Compressed data. A read from the card lands at IN_BASE, where the DMA has
 * the alignment that it needs, and the bytes left from the read before it go
 * directly below. */
#define IN_BASE      ((uint8_t *)(uintptr_t)0x31400000u)
#define IN_READ      0x00800000u

/* Decompressed data. A block can refer to the 64 KB before it, so that much
 * stays in front of the data that is not written yet. */
#define OUT_BUF      ((uint8_t *)(uintptr_t)0x31C00000u)
#define OUT_SIZE     0x01000000u
#define HISTORY      0x10000u

#define LZ4_MAGIC         0x184D2204u
#define LZ4_UNCOMPRESSED  0x80000000u
#define MAX_BLOCK         0x00400000u

#define MAX_BLOCKS   8192u      /* 1 GB of 128 KB blocks */

static struct {
    FIL f;
    uint32_t size, unread;      /* file bytes */
    uint8_t *in;
    uint32_t in_len;
    int write;                  /* NAND has the pads between two card reads */

    uint8_t *front, *end;       /* decompressed data not consumed yet */
    uint32_t history;           /* valid bytes in front of front */

    uint32_t pages, ppb;
    uint32_t page;              /* next page to consume */
    struct xxh32 hash;
    uint8_t marked[MAX_BLOCKS / 8u];    /* the image has a bad block marker */
    uint8_t data[MAX_BLOCKS / 8u];      /* the image has data in the block */
} s;

static uint32_t le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static int refill(void)
{
    uint32_t n = s.unread < IN_READ ? s.unread : IN_READ;
    int rc;

    if (n == 0)
        return NANDBACKUP_EFORMAT;
    if (s.write && nandtool_card_select())
        return NANDBACKUP_ECARD;
    memmove(IN_BASE - s.in_len, s.in, s.in_len);
    s.in = IN_BASE - s.in_len;
    rc = storage_read(&s.f, IN_BASE, n);
    if (s.write)
        nandtool_nand_select();
    if (rc)
        return NANDBACKUP_ECARD;
    s.in_len += n;
    s.unread -= n;
    return 0;
}

static int need(uint32_t n)
{
    while (s.in_len < n) {
        int rc = refill();

        if (rc)
            return rc;
    }
    return 0;
}

static void skip(uint32_t n)
{
    s.in += n;
    s.in_len -= n;
}

static int blank(const uint8_t *p, uint32_t bytes)
{
    const uint32_t *w = (const uint32_t *)p;

    for (uint32_t i = 0; i < bytes / 4u; i++)
        if (w[i] != 0xFFFFFFFFu)
            return 0;
    return 1;
}

static int consume(const uint8_t *p, uint32_t pages)
{
    struct nand_write_result res;
    int rc;

    if (s.page + pages > s.pages)
        return NANDBACKUP_EFORMAT;

    if (!s.write) {
        xxh32_update(&s.hash, p, pages * NAND_PAGE_RAW);
        for (uint32_t i = 0; i < pages; i += s.ppb) {
            uint32_t block = (s.page + i) / s.ppb;
            const uint8_t *b = p + i * NAND_PAGE_RAW;

            if (b[0x411] != 0xFF)
                s.marked[block / 8u] |= 1u << (block % 8u);
            if (!blank(b, s.ppb * NAND_PAGE_RAW))
                s.data[block / 8u] |= 1u << (block % 8u);
        }
        s.page += pages;
        return 0;
    }

    rc = nand_write_blocks(s.page, pages, s.ppb, NAND_WRITE, p, &res);
    if (rc) {
        log_puts("nandrestore: write failed at page ");
        log_dec(s.page + res.done);
        log_puts(", chip status ");
        log_dec(res.status);
        log_rc(rc);
        return NANDBACKUP_ENAND;
    }
    s.page += pages;
    return 0;
}

/* Makes room for one more block at s.end. */
static void compact(void)
{
    uint32_t keep = (uint32_t)(s.front - OUT_BUF);
    uint32_t len = (uint32_t)(s.end - s.front);

    if (s.end + MAX_BLOCK <= OUT_BUF + OUT_SIZE)
        return;
    if (keep > HISTORY)
        keep = HISTORY;
    memmove(OUT_BUF, s.front - keep, keep + len);
    s.front = OUT_BUF + keep;
    s.end = s.front + len;
    s.history = keep;
}

static int walk(const char *path, int write, int (*progress)(uint32_t),
                uint32_t *stored, int *has_checksum)
{
    uint32_t flg, max_block, header = 7;
    int block_checksum, rc;

    if (f_open(&s.f, path, FA_READ) != FR_OK)
        return NANDBACKUP_ECARD;
    s.size = s.unread = (uint32_t)f_size(&s.f);
    s.in = IN_BASE;
    s.in_len = 0;
    s.write = 0;
    s.front = s.end = OUT_BUF;
    s.history = 0;
    s.page = 0;

    rc = need(7);
    if (rc)
        goto out;
    flg = s.in[4];
    if (le32(s.in) != LZ4_MAGIC || (flg >> 6) != 1u || (flg & 1u) ||
        (s.in[5] >> 4) < 4u) {
        rc = NANDBACKUP_EFORMAT;
        goto out;
    }
    max_block = 1u << (8u + 2u * (s.in[5] >> 4));
    block_checksum = (flg >> 4) & 1u;
    *has_checksum = (flg >> 2) & 1u;
    if (flg & 8u)
        header += 8;
    rc = need(header);
    if (rc)
        goto out;
    skip(header);

    if (write) {
        s.write = 1;
        nandtool_nand_select();
    }

    for (;;) {
        uint32_t word, len;
        int n;

        rc = need(4);
        if (rc)
            break;
        word = le32(s.in);
        skip(4);
        if (word == 0)
            break;
        len = word & ~LZ4_UNCOMPRESSED;
        if (len > max_block) {
            rc = NANDBACKUP_EFORMAT;
            break;
        }
        rc = need(len + (block_checksum ? 4u : 0u));
        if (rc)
            break;

        compact();
        if (word & LZ4_UNCOMPRESSED) {
            memcpy(s.end, s.in, len);
            n = (int)len;
        } else {
            n = LZ4_decompress_safe_usingDict(
                (const char *)s.in, (char *)s.end, (int)len, (int)max_block,
                (const char *)s.end - s.history, (int)s.history);
        }
        if (n < 0) {
            rc = NANDBACKUP_EFORMAT;
            break;
        }
        skip(len + (block_checksum ? 4u : 0u));
        s.end += n;
        s.history += (uint32_t)n;
        if (s.history > HISTORY)
            s.history = HISTORY;

        while ((uint32_t)(s.end - s.front) >= BATCH_BYTES) {
            rc = consume(s.front, BATCH_PAGES);
            if (rc)
                goto out;
            s.front += BATCH_BYTES;
        }
        if (progress(1000u * (s.page / BATCH_PAGES) / (s.pages / BATCH_PAGES))) {
            rc = NANDBACKUP_CANCELLED;
            break;
        }
    }

    if (rc == 0) {
        uint32_t left = (uint32_t)(s.end - s.front);
        uint32_t pages = left / NAND_PAGE_RAW;

        if (left % NAND_PAGE_RAW || pages % s.ppb)
            rc = NANDBACKUP_EFORMAT;
        else if (pages)
            rc = consume(s.front, pages);
        if (rc == 0 && s.page != s.pages)
            rc = NANDBACKUP_EFORMAT;
    }
    if (rc == 0 && *has_checksum) {
        rc = need(4);
        if (rc == 0)
            *stored = le32(s.in);
    }

out:
    if (s.write && nandtool_card_select() && rc == 0)
        rc = NANDBACKUP_ECARD;
    s.write = 0;
    f_close(&s.f);
    return rc;
}

static int geometry(void)
{
    uint32_t id;
    int rc;

    nandtool_nand_select();
    rc = nand_read_id(&id);
    if (rc == 0)
        rc = nand_id_geometry(id, &s.pages, &s.ppb);
    if (nandtool_card_select())
        return NANDBACKUP_ECARD;
    if (rc || s.pages / s.ppb > MAX_BLOCKS)
        return NANDBACKUP_ENAND;
    return 0;
}

int nand_restore_check(const char *path, int (*progress)(uint32_t permille),
                       struct nand_restore_check *out)
{
    static char hex[12];
    const char *name = strrchr(path, '/');
    uint32_t stored = 0;
    int has_checksum = 0;
    int rc = geometry();

    if (rc)
        return rc;
    memset(s.marked, 0, sizeof(s.marked));
    memset(s.data, 0, sizeof(s.data));
    xxh32_init(&s.hash);
    rc = walk(path, 0, progress, &stored, &has_checksum);
    if (rc)
        return rc;

    out->checksum_ok = !has_checksum || xxh32_digest(&s.hash) == stored;
    lv_snprintf(hex, sizeof(hex), "%08x", (unsigned)stored);
    out->name_ok = !has_checksum || strstr(name ? name : path, hex) != NULL;

    out->lost = 0;
    nandtool_nand_select();
    for (uint32_t page = 0; page < s.pages; page += 64u * s.ppb) {
        struct nand_write_result res;
        uint32_t first = page / s.ppb;

        rc = nand_write_blocks(page, 64u * s.ppb, s.ppb, NAND_SCAN, NULL, &res);
        if (rc)
            break;
        for (uint32_t i = 0; i < 64u; i++) {
            uint32_t b = first + i;

            if ((res.bad[i >> 5] >> (i & 31u)) & 1u)
                out->lost += !((s.marked[b / 8u] >> (b % 8u)) & 1u) &&
                             ((s.data[b / 8u] >> (b % 8u)) & 1u);
        }
    }
    if (nandtool_card_select())
        return NANDBACKUP_ECARD;
    return rc ? NANDBACKUP_ENAND : NANDBACKUP_OK;
}

int nand_restore_write(const char *path, int (*progress)(uint32_t permille))
{
    uint32_t stored;
    int has_checksum;
    int rc = geometry();

    if (rc)
        return rc;
    return walk(path, 1, progress, &stored, &has_checksum);
}
