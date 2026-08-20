#include "nand.h"
#include "ecc.h"
#include "log.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* Sequencer bank 1. Bank 0 belongs to the bootrom. */
#define NF_FIFO_HEAD        0x2002A100u
#define NF_FIFO_NEXT        0x2002A104u
#define NF_CTRL_STA         REG32(0x2002A158u)
#define NF_TIMING_A         REG32(0x2002A15Cu)
#define NF_TIMING_B         REG32(0x2002A160u)
#define NF_SEQ_LAUNCH       0x40008400u

#define ECC_CTRL            REG32(0x2002B000u)
#define ECC_ERRPOS(i)       REG32(0x2002B004u + 4u * (i))
#define ECC_ERRPOS_COUNT    4u              /* t = 4 */

#define L2CTR_DMA_PATH_CFG  REG32(0x2002C084u)
#define L2CTR_BUF0_7_CFG    REG32(0x2002C088u)
#define L2CTR_ASSIGN_REG1   REG32(0x2002C090u)
#define L2CTR_STAT_REG1     REG32(0x2002C0A0u)
#define L2_NF_BUFFER        0x48000A00u     /* common buffer 5 */

#define ECC_DMA_DECODE      0x0C11049Au
#define NF_XFER_528         0x00107919u  /* ((528-1) << 11) | 0x118 | 1 */

#define ECC_STATUS_NO_ERR   0x04000000u
#define ECC_STATUS_NO_OK    0x08000000u
#define ECC_STATUS_END      0x00000040u
#define ECC_STATUS_DEC_RDY  0x01000000u

#define NF_WAIT_LIMIT       20000000u
#define PAGE_READ_TRIES     4u
#define MAX_BAD_SKIP        16u

static const uint8_t ipl_magic[8] = { 'I', 'M', 'G', 0, 'I', 'P', 'L', 0 };

/* Fixed for this part. Only pages_per_block comes from a runtime probe. */
#define PAGE_SIZE   2048u
#define CHUNKS      4u
#define ROW_CYCLES  3u
#define COL_CYCLES  2u

static uint32_t pages_per_block;
static uint32_t corrections;
static uint32_t retries;

uint32_t nand_corrections(void) { return corrections; }
uint32_t nand_retries(void) { return retries; }

void nand_init(void)
{
    NF_TIMING_A = 0x000F5AD1u;
    NF_TIMING_B = 0x000F5C5C;
    ECC_CTRL    = 0x00010000u;   /* NFC_EN alone */

    pages_per_block = 0u;
}

static void nf_seq_start(void)
{
    NF_CTRL_STA = (NF_CTRL_STA & 0x7FFFF3FFu) | NF_SEQ_LAUNCH;
}

static int nf_seq_wait(void)
{
    for (uint32_t i = 0; i < NF_WAIT_LIMIT; ++i) {
        if ((int32_t)NF_CTRL_STA < 0)   /* bit 31 = sequence complete */
            return 0;
    }
    return -1;
}

static volatile uint32_t *nf_emit_addr(volatile uint32_t *fifo,
                                       uint32_t row, uint32_t col)
{
    for (uint32_t i = 0; i < COL_CYCLES; ++i)
        *fifo++ = (((col >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    for (uint32_t i = 0; i < ROW_CYCLES; ++i)
        *fifo++ = (((row >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    return fifo;
}

static void l2_bind_nf_dma(void)
{
    L2CTR_DMA_PATH_CFG |= 0x30000000u;
    L2CTR_BUF0_7_CFG   |= 0x00200000u;
    L2CTR_ASSIGN_REG1   = (L2CTR_ASSIGN_REG1 & 0xFFFFF1FFu) | 0xA00u;
    L2CTR_BUF0_7_CFG   |= 0x20000000u;
}

/* Word stores only. An STRB into L2 SRAM writes the byte across the whole
 * word. n is a whole number of words and never exceeds one L2 buffer. */
static int l2_drain(void *dst, uint32_t n)
{
    for (uint32_t i = 0; ((L2CTR_STAT_REG1 >> 20) & 0xFu) < (n >> 6); ++i) {
        if (i >= NF_WAIT_LIMIT)
            return NAND_EL2_FILL;
    }

    const volatile uint32_t *src = (const volatile uint32_t *)L2_NF_BUFFER;
    uint32_t *d = (uint32_t *)dst;
    for (uint32_t i = 0; i < n / 4u; ++i)
        d[i] = src[i];
    return 0;
}

static int nand_read_raw(uint32_t page, uint32_t col, void *dst, uint32_t len)
{
    NF_CTRL_STA = 0;
    REG32(NF_FIFO_HEAD) = 0x64u;                    /* CMD 0x00 */
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)NF_FIFO_NEXT;
    fifo = nf_emit_addr(fifo, page, col);
    *fifo++ = 0x18464u;                             /* CMD 0x30 plus CMD_WAIT */
    *fifo   = 0x201u;                               /* wait ready, end */
    nf_seq_start();
    l2_bind_nf_dma();
    if (nf_seq_wait())
        return NAND_ESEQ_CMD;

    ECC_CTRL = (len << 7) | 0x100018u;              /* no DEC_EN: raw bytes */
    NF_CTRL_STA = 0;
    REG32(NF_FIFO_HEAD) = ((len - 1u) << 11) | 0x119u;
    nf_seq_start();
    if (nf_seq_wait())
        return NAND_ESEQ_DATA;

    int rc = l2_drain(dst, len);
    if (rc)
        return rc;

    for (uint32_t i = 0; i < NF_WAIT_LIMIT; ++i) {
        if (ECC_CTRL & ECC_STATUS_END) {
            ECC_CTRL |= ECC_STATUS_END;
            return 0;
        }
    }
    return NAND_EDMA_DONE;
}

enum { ECC_CLEAN, ECC_CORRECTABLE, ECC_UNCORRECTABLE };

/* Also acknowledges the engine, which is why it is not a pure predicate. */
static int ecc_classify(uint32_t status)
{
    if (status & ECC_STATUS_NO_ERR) {
        ECC_CTRL = status | ECC_STATUS_NO_ERR;
        return ECC_CLEAN;
    }
    if (status & ECC_STATUS_NO_OK) {
        ECC_CTRL = status | 3u;
        return ECC_UNCORRECTABLE;
    }
    return ECC_CORRECTABLE;
}

static int chunk_is_blank(const uint8_t *data)
{
    const uint32_t *p = (const uint32_t *)data;
    for (int i = 0; i < 128; ++i) {
        if (p[i] != 0xFFFFFFFFu)
            return 0;
    }
    return 1;
}

static int read_page_once(uint32_t page, uint8_t *dst)
{
    NF_CTRL_STA = 0;
    REG32(NF_FIFO_HEAD) = 0x64u;                    /* CMD 0x00 */
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)NF_FIFO_NEXT;
    fifo = nf_emit_addr(fifo, page, 0u);
    *fifo++ = 0x18064u;                             /* CMD 0x30 */
    *fifo   = 0x201u;                               /* wait ready, end */
    nf_seq_start();
    if (nf_seq_wait())
        return NAND_ESEQ_CMD;

    for (uint32_t c = 0; c < CHUNKS; ++c) {
        uint8_t *chunk = dst + (c << 9);

        l2_bind_nf_dma();
        ECC_CTRL = ECC_DMA_DECODE;
        NF_CTRL_STA = 0;
        REG32(NF_FIFO_HEAD) = NF_XFER_528;
        nf_seq_start();

        int rc = l2_drain(chunk, 0x200u);
        if (rc)
            return rc;
        if (nf_seq_wait())
            return NAND_ESEQ_DATA;

        const uint32_t done = ECC_STATUS_END | ECC_STATUS_DEC_RDY;
        uint32_t status = 0;
        uint32_t spins;
        for (spins = 0; spins < NF_WAIT_LIMIT; ++spins) {
            status = ECC_CTRL;
            if ((status & done) == done)
                break;
        }
        if (spins == NF_WAIT_LIMIT)
            return NAND_EDMA_DONE;

        /* Read the positions before the code below clears END. */
        uint32_t regs[ECC_ERRPOS_COUNT];
        for (uint32_t i = 0; i < ECC_ERRPOS_COUNT; ++i)
            regs[i] = ECC_ERRPOS(i);

        ECC_CTRL |= ECC_STATUS_END;

        switch (ecc_classify(status)) {
        case ECC_CORRECTABLE: {
            /* The engine does not correct in place. Tag corrections go to a
             * scratch buffer. */
            uint8_t tag[4] = { 0, 0, 0, 0 };
            if (ecc_apply(regs, ECC_ERRPOS_COUNT, chunk, tag, sizeof(tag)))
                return NAND_EBADPOS;
            for (uint32_t i = 0; i < ECC_ERRPOS_COUNT; ++i)
                corrections += (regs[i] != 0u);
            break;
        }
        case ECC_UNCORRECTABLE:
            if (!chunk_is_blank(chunk))
                return NAND_EUNCORRECTABLE;
            break;
        default:
            break;
        }
    }
    return 0;
}

static int nand_read_page(uint32_t page, void *dst)
{
    int rc = 0;

    for (uint32_t try = 0; try < PAGE_READ_TRIES; ++try) {
        rc = read_page_once(page, (uint8_t *)dst);
        if (rc == 0) {
            if (try)
                retries++;
            return 0;
        }
    }
    return rc;
}

static int block_is_bad(uint32_t block)
{
    uint32_t word;

    if (nand_read_raw(block * pages_per_block, 0x410u, &word, 4u))
        return 1;
    return ((word >> 8) & 0xFFu) != 0xFFu;   /* the marker at column 0x411 */
}

/* A wrong pages_per_block lands on a page without the IPL magic, so no wrong
 * candidate can pass this probe. */
int nand_probe_geometry(void)
{
    static const uint32_t candidates[] = { 64u, 128u, 32u, 256u };
    static uint8_t page[2048];

    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        pages_per_block = candidates[i];

        const uint32_t limit = IPL_START_BLOCK + 16u;
        uint32_t block = IPL_START_BLOCK;
        while (block < limit && block_is_bad(block))
            block++;
        if (block == limit)
            continue;
        if (nand_read_page(block * pages_per_block, page))
            continue;

        int match = 1;
        for (uint32_t k = 0; k < sizeof(ipl_magic); ++k)
            match &= (page[k] == ipl_magic[k]);
        if (match) {
            log_puts("nand: pages_per_block ");
            log_dec(pages_per_block);
            log_puts(", IPL at block ");
            log_dec(block);
            log_putc('\n');
            return 0;
        }
    }

    pages_per_block = 0u;
    return NAND_EPROBE;
}

/* This skips a bad block whole, the same rule that the OEM writer uses. Unlike
 * the stock nboot, a read error stops the load instead of a jump forward by
 * two blocks. */
int nand_load_image(void *dst, uint32_t start_block, uint32_t bytes)
{
    if (pages_per_block == 0u)
        return NAND_ENOGEOM;
    if (bytes == 0u || (bytes & (PAGE_SIZE - 1u)) != 0u)
        return NAND_ELEN;

    uint8_t *out = (uint8_t *)dst;
    uint32_t block = start_block;
    uint32_t remaining = bytes;
    uint32_t skipped = 0;

    while (remaining) {
        if (block_is_bad(block)) {
            if (++skipped > MAX_BAD_SKIP)
                return NAND_EBADRUN;
            log_puts("nand: skipping bad block ");
            log_dec(block);
            log_putc('\n');
            block++;
            continue;
        }

        uint32_t page = block * pages_per_block;
        for (uint32_t p = 0; p < pages_per_block && remaining; ++p) {
            int rc = nand_read_page(page + p, out);
            if (rc) {
                log_puts("nand: read failed at page ");
                log_dec(page + p);
                log_rc(rc);
                return rc;
            }
            out       += PAGE_SIZE;
            remaining -= PAGE_SIZE;
        }
        block++;
    }
    return 0;
}
