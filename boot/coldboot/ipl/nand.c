#include "nand.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* NF sequencer block 1 (block 0 at 0x2002A000-0x058 is bootrom's; nboot and
 * we use block 1 starting at 0x2002A100). */
#define NF_BASE             0x2002A000u
#define NF_FIFO_HEAD        0x2002A100u
#define NF_FIFO_NEXT        0x2002A104u
#define NF_CTRL_STA         REG32(0x2002A158u)

/* ECC / DMA controller (shared between block-0 and block-1 paths). */
#define ECC_CTRL            REG32(0x2002B000u)
#define ECC_CHUNK_STATUS(i) REG32(0x2002B008u + 4u * (i))

/* L2 SRAM controller. */
#define L2CTR_DMA_PATH_CFG  REG32(0x2002C084u)
#define L2CTR_BUF0_7_CFG    REG32(0x2002C088u)
#define L2CTR_ASSIGN_REG1   REG32(0x2002C090u)
#define L2CTR_STAT_REG1     REG32(0x2002C0A0u)

/* NF DMA target after l2_bind_nf_dma: L2 buffer slot 5 at offset 0xA00. */
#define L2_NF_BUFFER        0x48000A00u

/* Runtime NAND geometry, written by host nand_init() or nboot. */
#define NAND_PAGES_PER_BLOCK    REG32(0x30E00D00u)
#define NAND_PAGE_ADDR_CYCLES   REG32(0x30E00D04u)
#define NAND_CHUNKS_PER_PAGE    REG32(0x30E00D08u)
#define NAND_PAGE_SIZE          REG32(0x30E00D0Cu)
#define NAND_COL_ADDR_CYCLES    REG32(0x30E00D10u)

#define NF_WAIT_LIMIT           20000000u

/* NF FIFO micro-op encodings (bits [10:0]):
 *   0x062 output address byte    (parameter in bits [21:11])
 *   0x064 output command byte
 *   0x119 read data, count       (count-1 in bits [21:11])
 *   0x201 wait/delay
 * Sequencer launch value for block 1 is 0x40008400 (vs bootrom's 0x40000600
 * for block 0). */

static void nf_seq_start(uint32_t launch)
{
    NF_CTRL_STA = (NF_CTRL_STA & 0x7FFFF3FFu) | launch;
}

static int nf_seq_wait_done(void)
{
    for (uint32_t i = 0; i < NF_WAIT_LIMIT; ++i) {
        if ((int32_t)NF_CTRL_STA < 0)
            return 0;  /* bit 31 set when sequence finishes */
    }
    return -10;
}

/* Append col-address bytes then row-address bytes to FIFO at `fifo`.
 * Number of cycles per axis comes from runtime params (v5/v6 in nboot). */
static volatile uint32_t *nf_emit_addr_cycles(volatile uint32_t *fifo,
                                              uint32_t row, uint32_t col)
{
    uint32_t n_col = NAND_COL_ADDR_CYCLES;
    for (uint32_t i = 0; i < n_col; ++i)
        *fifo++ = (((col >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    uint32_t n_row = NAND_PAGE_ADDR_CYCLES;
    for (uint32_t i = 0; i < n_row; ++i)
        *fifo++ = (((row >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    return fifo;
}

/* Bind NF DMA path to L2 buffer slot 5 (0x48000A00). */
static void l2_bind_nf_dma(void)
{
    L2CTR_DMA_PATH_CFG |= 0x30000000u;
    L2CTR_BUF0_7_CFG   |= 0x00200000u;
    L2CTR_ASSIGN_REG1   = (L2CTR_ASSIGN_REG1 & 0xFFFFF1FFu) | 0xA00u;
    L2CTR_BUF0_7_CFG   |= 0x20000000u;
}

/* Pull `n` bytes (<= 512) from the NF L2 buffer into `dst`. Polls L2CTR
 * status counter until the buffer has >= n/64 chunks. */
static int l2_copy_from_buf(void *dst, uint32_t n)
{
    if (n > 0x200u)
        return -11;
    for (uint32_t i = 0; ((L2CTR_STAT_REG1 >> 20) & 0xFu) < (n >> 6); ++i) {
        if (i >= NF_WAIT_LIMIT)
            return -12;
    }
    const volatile uint32_t *src = (const volatile uint32_t *)L2_NF_BUFFER;
    uint8_t *d8 = (uint8_t *)dst;
    uint32_t whole = n & 0x3FCu;
    for (uint32_t i = 0; i < whole; i += 4u) {
        uint32_t w = src[i >> 2];
        *(uint32_t *)(d8 + i) = w;
    }
    uint32_t rem = n & 3u;
    if (rem) {
        uint32_t w = src[whole >> 2];
        for (uint32_t i = 0; i < rem; ++i)
            d8[whole + i] = (uint8_t)(w >> (8u * i));
    }
    return 0;
}

/* Classify ECC_CTRL status after a chunk DMA completes.
 * 1 = ECC clean (no errors), continue
 * 2 = ECC reports correctable errors (caller invokes correction path)
 * 3 = ECC uncorrectable
 *
 * Bit 26 of ECC_CTRL appears to flag clean reads (cleared by writing 1).
 * Bit 27 flags uncorrectable. Anything else falls through as correctable.
 */
static int nf_classify_dma_result(uint32_t status)
{
    if (status & 0x04000000u) {
        ECC_CTRL = status | 0x04000000u;
        return 1;
    }
    if (status & 0x08000000u) {
        ECC_CTRL = status | 3u;
        return 3;
    }
    return 2;
}

/* Software ECC correction: not yet ported from nboot's ecc_apply_corrections.
 * Returns 1 to signal "could not correct"; callers treat this as a hard
 * read failure. For healthy NAND (no bit flips) this path is never taken. */
static int ecc_fix_page_from_hw_regs(uint8_t *data, uint8_t *oob)
{
    (void)data;
    (void)oob;
    return 1;
}

/* Issue a NAND READ command for `page_no` and pull `chunks` chunks of 512
 * bytes each through the ECC engine into `dst`. `oob_dst` is scratch for
 * per-chunk OOB used by the (not yet implemented) correction path. */
static int nf_read_page_with_ecc(uint32_t page_no, uint8_t *dst,
                                 uint8_t *oob_dst, uint32_t chunks)
{
    NF_CTRL_STA = 0;
    REG32(NF_FIFO_HEAD) = 0x64u;  /* output command byte 0x00 (READ first) */
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)NF_FIFO_NEXT;
    fifo = nf_emit_addr_cycles(fifo, page_no, 0u);
    if (chunks != 1u) {
        *fifo++ = 0x18000u | 0x64u;  /* output command byte 0x30 (READ confirm) */
    }
    *fifo++ = 0x201u;  /* wait until ready */
    nf_seq_start(0x40008400u);
    int rc = nf_seq_wait_done();
    if (rc)
        return rc;

    for (uint32_t c = 0; c < chunks; ++c) {
        l2_bind_nf_dma();
        ECC_CTRL = 0x0C11049Au;  /* verbatim from nboot @0x30000634-0x3000064C */
        NF_CTRL_STA = 0;
        REG32(NF_FIFO_HEAD) = 0x107919u;  /* read 528 bytes (512 data + 16 ECC) */
        nf_seq_start(0x40008400u);
        rc = l2_copy_from_buf(dst + (c << 9), 0x200u);
        if (rc)
            return rc;
        rc = nf_seq_wait_done();
        if (rc)
            return rc;

        uint32_t status;
        uint32_t spins = 0;
        for (;;) {
            status = ECC_CTRL;
            if (((status >> 6) & 1u) && (status & 0x01000000u))
                break;
            if (++spins >= NF_WAIT_LIMIT)
                return -13;
        }
        ECC_CTRL = status | 0x40u;  /* clear DMA done */

        int kind = nf_classify_dma_result(status) & 0xFFu;
        if (kind == 2) {
            int rc = ecc_fix_page_from_hw_regs(dst + (c << 9),
                                               oob_dst + 4u * c);
            if (rc)
                return rc;
            continue;
        }
        if (kind == 3) {
            /* Check if the chunk is an erased page (all 0xFF). */
            const uint32_t *p32 = (const uint32_t *)(dst + (c << 9));
            for (int i = 0; i < 128; ++i) {
                if (p32[i] != 0xFFFFFFFFu)
                    return 3;
            }
            /* All 0xFF, treat as clean. (nboot also walks 8 OOB bytes here;
             * we skip that until OOB is plumbed through.) */
            continue;
        }
        /* kind == 1: clean. */
    }
    return 0;
}

static int nand_geometry_valid(uint32_t page_size, uint32_t pages_per_block,
                               uint32_t chunks)
{
    if (page_size != 0x200u && page_size != 0x800u && page_size != 0x1000u)
        return 0;
    if (chunks == 0 || chunks > 8u || chunks != (page_size >> 9))
        return 0;
    if (pages_per_block == 0 || pages_per_block > 256u)
        return 0;
    if (NAND_COL_ADDR_CYCLES == 0 || NAND_COL_ADDR_CYCLES > 2u)
        return 0;
    if (NAND_PAGE_ADDR_CYCLES == 0 || NAND_PAGE_ADDR_CYCLES > 5u)
        return 0;
    return 1;
}

int nand_read_page(uint32_t page_no, void *dst, void *oob_dst)
{
    return nf_read_page_with_ecc(page_no, (uint8_t *)dst,
                                  (uint8_t *)oob_dst,
                                  NAND_CHUNKS_PER_PAGE);
}

int nand_load_partition(void *dst, uint32_t start_block, uint32_t max_bytes)
{
    static uint8_t oob_scratch[64];
    uint32_t page_size = NAND_PAGE_SIZE;
    uint32_t pages_per_block = NAND_PAGES_PER_BLOCK;
    uint32_t chunks = NAND_CHUNKS_PER_PAGE;
    if (!nand_geometry_valid(page_size, pages_per_block, chunks))
        return -20;

    uint32_t page = start_block * pages_per_block;
    uint8_t *out = (uint8_t *)dst;

    while (max_bytes >= page_size) {
        int rc = nf_read_page_with_ecc(page, out, oob_scratch,
                                        chunks);
        if (rc != 0)
            return rc;
        out      += page_size;
        max_bytes -= page_size;
        page++;
    }
    return 0;
}
