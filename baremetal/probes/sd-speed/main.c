#include <stddef.h>
#include "sd.c"

static uint32_t tick_total, tick_last;

#define T3_CTRL       REG32(0x08000020u)
#define T3_LIVE       REG32(0x08000108u)
#define T_MASK        0x03FFFFFFu
#define T_EN          (1u << 26)
#define TICKS_PER_MS  12000u
#define IO_TIMEOUT    (2000u * TICKS_PER_MS)

static void tick_start(void)
{
    T3_CTRL = T_MASK;
    T3_CTRL = T_MASK | T_EN;
    tick_last = T3_LIVE & T_MASK;
    tick_total = 0;
}

static uint32_t ticks(void)
{
    uint32_t raw = T3_LIVE & T_MASK;

    tick_total += (tick_last - raw) & T_MASK;
    tick_last = raw;
    return tick_total;
}

static int counted_read_block(uint32_t lba, void *dst)
{
    int rc = sd_read_block(lba, dst);

    ticks();
    return rc;
}

#define sd_read_block counted_read_block
#include "fat.c"
#undef sd_read_block

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;

    while (n--)
        *p++ = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d;
    const uint8_t *q = s;

    while (n--)
        *p++ = *q++;
    return d;
}

#define MCI_DATACNT   MCI(0x30)
#define MCI_FIFO      MCI(0x40)
#define STA_RXACTIVE  (1u << 11)
#define STA_FIFOFULL  (1u << 12)

#define L2(off)       REG32(0x2002C000u + (off))
#define L2_DMAREQ     L2(0x80)
#define DMA_BUF       2u
#define L2_DMA_ADDR   L2(DMA_BUF * 4u)
#define L2_DMA_COUNT  L2(0x40u + DMA_BUF * 4u)
#define L2_REQ        (1u << (24u + DMA_BUF))
#define L2_REQ_MASK   0xFFFF0000u
#define L2_FRACTION   (1u << 9)
#define DMA_CHUNK     8192u

#define REF_RAW       0x31200000u
#define DST_RAW       0x31600000u
#define RAW_MAX_SECT  8192u
#define REF_FILE      0x31A00000u
#define DST_FILE      0x31C00000u
#define FILE_MAX      0x00200000u

#define RUNS_MAX      4096u

struct params {
    char file[12];
    uint32_t raw_lba;
    uint32_t raw_sectors;
    uint32_t skip;
};

struct params params = { "BOOT    BIN", 0, 2048, 0 };

struct rec {
    const char *name;
    uint32_t icache;
    int32_t rc;
    uint32_t init;
    uint32_t first;
    uint32_t single;
    uint32_t hs;
    uint32_t work;
    uint32_t bytes;
    uint32_t mismatch;
    uint32_t nbad;
    uint32_t got;
    uint32_t want;
    uint32_t clock;
};

struct rec recs[32];
uint32_t nrec;
uint32_t entry_c1;
uint32_t raw_lba_used;
uint32_t file_size;
uint32_t file_runs;

static uint32_t scratch[128];
static uint32_t saved_dmareq;
static struct { uint32_t lba, count; } runs[RUNS_MAX];

enum kind { K_FILE_CMD17, K_RAW_CMD17, K_RAW_PIO, K_RAW_DMA, K_FILE_DMA };

static const struct phase {
    const char *name;
    uint8_t kind;
    uint8_t hs;
    uint16_t req;
} phases[] = {
    { "file-cmd17",      K_FILE_CMD17, 0, 1 },
    { "raw-cmd17",       K_RAW_CMD17,  0, 1 },
    { "raw-pio18-64",    K_RAW_PIO,    0, 64 },
    { "raw-dma-8",       K_RAW_DMA,    0, 8 },
    { "raw-dma-64",      K_RAW_DMA,    0, 64 },
    { "raw-dma-1024",    K_RAW_DMA,    0, 1024 },
    { "file-dma",        K_FILE_DMA,   0, 1024 },
    { "hs-raw-cmd17",    K_RAW_CMD17,  1, 1 },
    { "hs-raw-dma-1024", K_RAW_DMA,    1, 1024 },
    { "hs-file-dma",     K_FILE_DMA,   1, 1024 },
};

static void icache_set(int on)
{
    uint32_t c, z = 0;

    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (c));
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r" (z));
    c = on ? c | 0x1000u : c & ~0x1000u;
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r" (c));
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r" (z));
}

static uint32_t card_arg(uint32_t lba)
{
    return g_sdhc ? lba : lba << 9;
}

static int hs_switch(void)
{
    uint32_t buf[16], sta, seen = 0, t0;

    sd_stop_transfer();
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = 64u;
    MCI_DATACTRL  = (64u << 16) | DPSM_BUS_4BIT | DPSM_DIR_READ | DPSM_ENABLE;
    MCI_ARG  = 0x80FFFFF1u;
    MCI_MASK = 0x1FFu;
    MCI_CMD  = CPSM_ENABLE | CPSM_RESPONSE | CPSM_WITHDATA | (6u << 1);
    if (!(wait_sta(RESP_MASK) & STA_RESPEND)) {
        sd_stop_transfer();
        return -60;
    }
    if (MCI_RESP0 & R1_ERRORS) {
        sd_stop_transfer();
        return -61;
    }
    t0 = ticks();
    for (uint32_t w = 0; w < 16u; w++) {
        for (;;) {
            sta = MCI_STA;
            seen |= sta;
            if (sta & STA_DATA_ERROR) {
                sd_stop_transfer();
                return -62;
            }
            if ((sta & (STA_FIFOFULL | STA_RXACTIVE)) ==
                (STA_FIFOFULL | STA_RXACTIVE))
                break;
            if (ticks() - t0 > IO_TIMEOUT) {
                sd_stop_transfer();
                return -63;
            }
        }
        buf[w] = MCI_FIFO;
    }
    while (!(seen & STA_DATAEND) && ticks() - t0 <= IO_TIMEOUT)
        seen |= MCI_STA;
    sd_stop_transfer();
    if ((buf[4] & 0xFu) != 1u)
        return -64;
    sd_set_clock_divider(2u);
    return sd_wait_ready() ? -65 : 0;
}

static int pio18(uint32_t lba, uint32_t n, uint32_t *dst)
{
    const volatile uint32_t *src = MMIO_PTR(SD_L2_BUF_ADDR);
    uint32_t t0;
    int rc = 0;

    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL  = 0;
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));

    MCI_DMACTRL   = MCI_DMACTRL_L2;
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = n * 512u;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);
    MCI_ARG  = card_arg(lba);
    MCI_MASK = 0x1FFu;
    MCI_CMD  = CPSM_ENABLE | CPSM_RESPONSE | (18u << 1);
    if (!(wait_sta(RESP_MASK) & STA_RESPEND)) {
        rc = -18;
        goto out;
    }
    (void)MCI_RESP0;

    for (uint32_t b = 0; b < n; b++) {
        t0 = ticks();
        while (((L2_BUFSTAT1 >> (SD_L2_BUF_ID * 4)) & 0xFu) < 8u) {
            if (MCI_STA & STA_DATA_ERROR) {
                rc = -20;
                goto out;
            }
            if (ticks() - t0 > IO_TIMEOUT) {
                rc = -22;
                goto out;
            }
        }
        for (uint32_t w = 0; w < 128u; w++)
            dst[b * 128u + w] = src[w];
    }
    t0 = ticks();
    while (!(MCI_STA & STA_DATAEND)) {
        if (ticks() - t0 > IO_TIMEOUT) {
            rc = -21;
            break;
        }
    }
out:
    sd_stop_transfer();
    (void)send_resp(12, 0, CPSM_RESPONSE, 0);
    if (sd_wait_ready() && !rc)
        rc = -13;
    return rc;
}

static void dma_chunk(uint32_t addr, uint32_t bytes)
{
    L2_DMA_ADDR  = addr;
    L2_DMA_COUNT = bytes / 64u;
    L2_CONBUF0_7 = (L2_CONBUF0_7 & ~(1u << (8u + DMA_BUF))) |
                   (1u << DMA_BUF) | (1u << (16u + DMA_BUF));
    L2_DMAREQ = (L2_DMAREQ & ~(L2_FRACTION | L2_REQ_MASK)) | 1u | L2_REQ;
}

static int dma18(uint32_t lba, uint32_t n, uint32_t dst)
{
    uint32_t total = n * 512u, sent, r1, sta, seen = 0, t0;
    int rc = 0;

    if (send_resp(23, n, CPSM_RESPONSE, &r1) < 0 || (r1 & R1_ERRORS))
        return -23;

    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL  = 0;
    SYSCTRL(0x0C) &= ~(1u << 3);
    L2_DMAREQ = (L2_DMAREQ & ~(L2_FRACTION | L2_REQ_MASK)) | 1u;
    L2_BUFASSIGN1 = (L2_BUFASSIGN1 & ~(0x7u << 12)) | (DMA_BUF << 12);
    L2_CONBUF0_7 = (L2_CONBUF0_7 & ~(1u << (8u + DMA_BUF))) |
                   (1u << DMA_BUF) | (1u << (16u + DMA_BUF));
    L2_CONBUF0_7 |= 1u << (24u + DMA_BUF);

    sent = total < DMA_CHUNK ? total : DMA_CHUNK;
    dma_chunk(dst, sent);

    MCI_DMACTRL   = MCI_DMACTRL_L2;
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = total;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);
    MCI_ARG  = card_arg(lba);
    MCI_MASK = 0x1FFu;
    MCI_CMD  = CPSM_ENABLE | CPSM_RESPONSE | CPSM_WITHDATA | (18u << 1);
    if (!(wait_sta(RESP_MASK) & STA_RESPEND)) {
        rc = -18;
        goto out;
    }
    if (MCI_RESP0 & R1_ERRORS) {
        rc = -19;
        goto out;
    }

    t0 = ticks();
    for (;;) {
        sta = MCI_STA;
        seen |= sta;
        if (sta & STA_DATA_ERROR) {
            rc = -20;
            break;
        }
        if (!(L2_DMAREQ & L2_REQ)) {
            if (sent < total) {
                uint32_t b = total - sent < DMA_CHUNK ? total - sent
                                                      : DMA_CHUNK;

                dma_chunk(dst + sent, b);
                sent += b;
                t0 = ticks();
                continue;
            }
            if (seen & STA_DATAEND) {
                rc = MCI_DATACNT ? -21 : 0;
                break;
            }
        }
        if (ticks() - t0 > IO_TIMEOUT) {
            rc = -22;
            break;
        }
    }
out:
    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DATALEN  = 0;
    MCI_DMACTRL  = 0;
    MCI_MASK     = 0;
    if (L2_DMAREQ & L2_REQ) {
        L2_DMAREQ = (L2_DMAREQ & ~(L2_FRACTION | L2_REQ_MASK)) | 1u;
        L2_DMA_COUNT = 0;
    }
    L2_CONBUF0_7 |= 1u << (24u + DMA_BUF);
    if (rc) {
        (void)send_resp(12, 0, CPSM_RESPONSE, 0);
        (void)sd_wait_ready();
    }
    return rc;
}

static int read_raw(const struct phase *p, uint32_t dst)
{
    uint32_t n = params.raw_sectors;

    for (uint32_t off = 0; off < n;) {
        uint32_t k = n - off < p->req ? n - off : p->req;
        uint32_t lba = raw_lba_used + off, addr = dst + off * 512u;
        int rc;

        if (p->kind == K_RAW_CMD17)
            rc = sd_read_block(lba, (void *)(uintptr_t)addr);
        else if (p->kind == K_RAW_PIO)
            rc = pio18(lba, k, (uint32_t *)(uintptr_t)addr);
        else
            rc = dma18(lba, k, addr);
        ticks();
        if (rc)
            return rc;
        off += p->kind == K_RAW_CMD17 ? 1u : k;
    }
    return 0;
}

static int add_run(uint32_t lba, uint32_t count, uint32_t req)
{
    uint32_t i = file_runs;

    if (i && runs[i - 1].lba + runs[i - 1].count == lba &&
        runs[i - 1].count + count <= req) {
        runs[i - 1].count += count;
        return 0;
    }
    if (i == RUNS_MAX)
        return -101;
    runs[i].lba = lba;
    runs[i].count = count;
    file_runs = i + 1;
    return 0;
}

/* Walks the cluster chain with the fat.c helpers, merges contiguous clusters
 * into requests of at most req sectors, and reads them with dma18(). */
static int read_file_dma(const struct phase *p, uint32_t dst)
{
    uint32_t first, size, cluster, need, off = 0;
    int rc;

    rc = fat_load_file(params.file, (void *)(uintptr_t)dst, 0, &size);
    if (rc != -9)
        return rc ? rc : -100;
    rc = find_in_root(params.file, &first, &size);
    if (rc)
        return rc;

    file_runs = 0;
    need = (size + 511u) / 512u;
    for (cluster = first; need && cluster >= 2u &&
         cluster < (v.is_fat32 ? 0x0FFFFFF8u : 0xFFF8u);) {
        uint32_t take = need < v.sectors_per_cluster ? need
                                                     : v.sectors_per_cluster;

        rc = add_run(v.data_start_lba + (cluster - 2u) * v.sectors_per_cluster,
                     take, p->req);
        if (rc)
            return rc;
        need -= take;
        if (need)
            cluster = fat_entry(cluster);
    }
    if (need)
        return -10;

    for (uint32_t i = 0; i < file_runs; i++) {
        rc = dma18(runs[i].lba, runs[i].count, dst + off);
        ticks();
        if (rc)
            return rc;
        off += runs[i].count * 512u;
    }
    return 0;
}

static uint32_t compare(uint32_t a, uint32_t b, uint32_t bytes)
{
    const volatile uint8_t *x = (const volatile uint8_t *)(uintptr_t)a;
    const volatile uint8_t *y = (const volatile uint8_t *)(uintptr_t)b;

    for (uint32_t i = 0; i < bytes; i++)
        if (x[i] != y[i])
            return i;
    return 0xFFFFFFFFu;
}

static uint32_t count_bad(uint32_t a, uint32_t b, uint32_t bytes)
{
    const volatile uint8_t *x = (const volatile uint8_t *)(uintptr_t)a;
    const volatile uint8_t *y = (const volatile uint8_t *)(uintptr_t)b;
    uint32_t n = 0;

    for (uint32_t i = 0; i < bytes; i++)
        n += x[i] != y[i];
    return n;
}

static void fill(uint32_t addr, uint32_t bytes)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)addr;

    for (uint32_t i = 0; i < bytes / 4u; i++)
        p[i] = 0xA5A5A5A5u;
}

static uint32_t first_fat_partition(const uint8_t *mbr)
{
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = mbr + 0x1BE + 16 * i;
        uint8_t type = e[4];

        if (type == 0x04u || type == 0x06u || type == 0x0Bu ||
            type == 0x0Cu || type == 0x0Eu)
            return rd32(e + 8);
    }
    return 0;
}

static void run_phase(const struct phase *p, int ic, int is_ref)
{
    struct rec *r = &recs[nrec++];
    int file = p->kind == K_FILE_CMD17 || p->kind == K_FILE_DMA;
    uint32_t dst = file ? (is_ref ? REF_FILE : DST_FILE)
                        : (is_ref ? REF_RAW : DST_RAW);
    uint32_t len = file ? (file_size ? (file_size + 511u) & ~511u : FILE_MAX)
                        : params.raw_sectors * 512u;
    uint32_t t;
    int rc;

    r->name = p->name;
    r->icache = ic;
    r->mismatch = 0xFFFFFFFFu;
    fill(dst, len);
    icache_set(ic);

    t = ticks();
    rc = sd_init();
    r->init = ticks() - t;
    if (rc)
        goto done;

    t = ticks();
    rc = sd_read_block(0, scratch);
    r->first = ticks() - t;
    if (rc)
        goto done;
    if (!raw_lba_used)
        raw_lba_used = params.raw_lba ? params.raw_lba
                                      : first_fat_partition((uint8_t *)scratch);

    t = ticks();
    rc = sd_read_block(raw_lba_used, scratch);
    r->single = ticks() - t;
    if (rc)
        goto done;

    if (p->hs) {
        t = ticks();
        rc = hs_switch();
        r->hs = ticks() - t;
        if (rc)
            goto done;
    }

    t = ticks();
    if (p->kind == K_FILE_CMD17) {
        uint32_t size = 0;

        rc = fat_load_file(params.file, (void *)(uintptr_t)dst, FILE_MAX,
                           &size);
        if (!rc && is_ref)
            file_size = size;
    } else if (p->kind == K_FILE_DMA) {
        rc = read_file_dma(p, dst);
    } else {
        rc = read_raw(p, dst);
    }
    r->work = ticks() - t;
    r->clock = MCI_CLOCK;
    if (rc)
        goto done;

    r->bytes = file ? file_size : params.raw_sectors * 512u;
    if (!is_ref)
        r->mismatch = compare(dst, file ? REF_FILE : REF_RAW, r->bytes);
    if (r->mismatch != 0xFFFFFFFFu) {
        r->got = REG32(dst + (r->mismatch & ~3u));
        r->want = REG32((file ? REF_FILE : REF_RAW) + (r->mismatch & ~3u));
        r->nbad = count_bad(dst, file ? REF_FILE : REF_RAW, r->bytes);
    }

done:
    r->rc = rc;
    icache_set(0);
    L2_DMAREQ = saved_dmareq & ~L2_REQ_MASK;
}

int main(void)
{
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (entry_c1));
    if (params.raw_sectors == 0 || params.raw_sectors > RAW_MAX_SECT)
        params.raw_sectors = 2048u;
    saved_dmareq = L2_DMAREQ;
    tick_start();

    for (uint32_t i = 0; i < sizeof(phases) / sizeof(phases[0]); i++)
        for (int ic = 0; ic < 2; ic++)
            if (!(params.skip & (1u << i)))
                run_phase(&phases[i], ic,
                      ic == 0 && (i == 0 || i == 1));

    sd_release_pins();
    T3_CTRL = 0;
    return 0;
}
