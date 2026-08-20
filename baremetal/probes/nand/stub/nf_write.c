#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define NF_SEQ   0x2002A100u
#define NF_CTRL  0x2002A158u
#define NF_TIM_A 0x2002A15Cu
#define NF_TIM_B 0x2002A160u
#define NF_INFO  0x2002A150u /* reg20, where a status or ID read lands */
#define NF_DMA   0x2002B000u

#define L2_DMA_PATH 0x2002C084u
#define L2_BUF_CFG  0x2002C088u
#define L2_ASSIGN   0x2002C090u
#define L2_STAT     0x2002C0A0u
#define L2_BUF5     0x48000A00u

#define HDR  ((volatile uint32_t *)(uintptr_t)0x48001100u)
#define HDR_WORDS 32u
#define SCRATCH ((volatile uint8_t *)(uintptr_t)0x48000C00u)

#define COL_CYCLES 2u
#define ROW_CYCLES 3u
#define CHUNKS     4u

#define ROM_NF_BOOT_HW_INIT ((void (*)(void))0x2648u)
#define SPIN 0x400000u

#ifndef PROBE_MODE
#define PROBE_MODE 0
#endif
#ifndef PROBE_BLOCK
#define PROBE_BLOCK 1u
#endif
#ifndef PROBE_PPB
#define PROBE_PPB 64u          /* cat: 64 pages per block, gray: 128 */
#endif
#ifndef PROBE_PAGE_IN_BLOCK
#define PROBE_PAGE_IN_BLOCK 0u
#endif
#ifndef PROBE_TIM_A
#define PROBE_TIM_A 0x000F5AD1u
#endif
#ifndef PROBE_TIM_B
#define PROBE_TIM_B 0x000F5C5Cu
#endif

#if (PROBE_BLOCK != 1u) && !defined(ALLOW_ANY_BLOCK)
#error "target block is not NBT block 1; define ALLOW_ANY_BLOCK if that is deliberate"
#endif

#define ST_FAIL     0x01u
#define ST_READY    0x40u
#define ST_NOT_WP   0x80u

enum {
    ST_OK = 0,
    ST_ERR_SEQ_STATUS = 1,
    ST_ERR_WP         = 2,
    ST_ERR_SEQ_ERASE  = 3,
    ST_ERR_ERASE_FAIL = 4,
    ST_ERR_NOT_BLANK  = 5,
    ST_ERR_SEQ_PROG   = 6,
    ST_ERR_PROG_FAIL  = 7,
    ST_ERR_SEQ_READ   = 8,
    ST_ERR_L2         = 9,
    ST_ERR_DMA        = 10,
    ST_ERR_VERIFY     = 11,
};

static void seq_start(void)
{
    REG32(NF_CTRL) = (REG32(NF_CTRL) & 0x7FFFF3FFu) | 0x40008400u;
}

static int seq_wait(void)
{
    for (uint32_t i = 0; i < SPIN; i++)
        if (REG32(NF_CTRL) & 0x80000000u)
            return 1;
    return 0;
}

__attribute__((unused)) static void l2_bind_nf_dma(void)
{
    REG32(L2_DMA_PATH) |= 0x30000000u;
    REG32(L2_BUF_CFG) |= 0x00200000u;
    REG32(L2_ASSIGN) = (REG32(L2_ASSIGN) & ~0xE00u) | 0xA00u;
    REG32(L2_BUF_CFG) |= 0x20000000u;
}

/* L2 common buffer config [15:8]: per-buffer direction. Must set for writes. */
__attribute__((unused)) static void l2_set_dir_write(int on)
{
    if (on)
        REG32(L2_BUF_CFG) |= (1u << (8 + 5));
    else
        REG32(L2_BUF_CFG) &= ~(1u << (8 + 5));
}


__attribute__((unused)) static volatile uint32_t *emit_addr(volatile uint32_t *slot, uint32_t row,
                                    uint32_t col, int with_col)
{
    if (with_col)
        for (uint32_t i = 0; i < COL_CYCLES; i++)
            *slot++ = (((col >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    for (uint32_t i = 0; i < ROW_CYCLES; i++)
        *slot++ = (((row >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    return slot;
}

static int read_status(uint32_t *out)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = (0x70u << 11) | 0x64u;
    *slot = 0x58u | 0x001u;
    seq_start();
    if (!seq_wait())
        return 0;
    *out = REG32(NF_INFO) & 0xFFu;
    return 1;
}

__attribute__((unused)) static int erase_block(uint32_t row, uint32_t *status)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = (0x60u << 11) | 0x64u;            /* BLOCK ERASE */
    slot = emit_addr(slot, row, 0, 0);          /* row cycles only */
    *slot++ = (0xD0u << 11) | 0x64u;            /* ERASE CONFIRM */
    *slot = 0x201u;                             /* wait ready, end */
    seq_start();
    if (!seq_wait())
        return 0;
    return read_status(status);
}


__attribute__((unused)) static uint32_t pattern_word(uint32_t i)
{
    return i * 0x01010101u + 0xA5A5A5A5u;
}

/* Word stores only: STRB into L2 SRAM splatters the byte across the word. */
__attribute__((unused)) static int program_chunk(uint32_t chunk)
{
    volatile uint32_t *buf = (volatile uint32_t *)(uintptr_t)L2_BUF5;

    for (uint32_t w = 0; w < 128u; w++)
        buf[w] = pattern_word(chunk * 128u + w);

    REG32(NF_DMA) = (512u << 7) | 0x10001Cu;    /* as the read path, plus DIR_WRITE */
    REG32(NF_CTRL) = 0;
    REG32(NF_SEQ) = ((512u - 1u) << 11) | 0x128u | 0x001u;
    seq_start();
    if (!seq_wait())
        return 0;

    for (uint32_t i = 0; i < SPIN; i++) {
        if (REG32(NF_DMA) & 0x40u) {
            REG32(NF_DMA) |= 0x40u;
            return 1;
        }
    }
    return 0;
}

__attribute__((unused)) static int program_page(uint32_t row, uint32_t *status)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    l2_bind_nf_dma();
    l2_set_dir_write(1);

    REG32(NF_CTRL) = 0;
    *slot++ = (0x80u << 11) | 0x64u;            /* PAGE PROGRAM */
    slot = emit_addr(slot, row, 0, 1);
    slot[-1] |= 0x001u;                         /* last address cycle ends it */
    seq_start();
    if (!seq_wait()) {
        l2_set_dir_write(0);
        return 0;
    }

    for (uint32_t c = 0; c < CHUNKS; c++) {
        if (!program_chunk(c)) {
            l2_set_dir_write(0);
            return 0;
        }
    }

    slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;
    REG32(NF_CTRL) = 0;
    *slot++ = (0x10u << 11) | 0x64u;            /* PROGRAM CONFIRM */
    *slot = 0x201u;
    seq_start();
    if (!seq_wait()) {
        l2_set_dir_write(0);
        return 0;
    }

    l2_set_dir_write(0);
    return read_status(status);
}


__attribute__((unused)) static int read_raw(uint32_t row, uint32_t col, volatile uint8_t *dst, uint32_t len)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = 0x64u;
    slot = emit_addr(slot, row, col, 1);
    *slot++ = 0x18464u;
    *slot = 0x201u;
    seq_start();
    l2_bind_nf_dma();
    if (!seq_wait())
        return ST_ERR_SEQ_READ;

    REG32(NF_DMA) = (len << 7) | 0x100018u;
    REG32(NF_CTRL) = 0;
    REG32(NF_SEQ) = ((len - 1u) << 11) | 0x119u;
    seq_start();
    if (!seq_wait())
        return ST_ERR_SEQ_READ;

    uint32_t i;
    for (i = 0; i < SPIN; i++)
        if (((REG32(L2_STAT) >> 20) & 0xFu) >= (len >> 6))
            break;
    if (i == SPIN)
        return ST_ERR_L2;

    volatile uint32_t *d = (volatile uint32_t *)dst;
    const volatile uint32_t *s = (const volatile uint32_t *)(uintptr_t)L2_BUF5;
    for (uint32_t w = 0; w < (len >> 2); w++)
        d[w] = s[w];

    for (i = 0; i < SPIN; i++) {
        if (REG32(NF_DMA) & 0x40u) {
            REG32(NF_DMA) |= 0x40u;
            return ST_OK;
        }
    }
    return ST_ERR_DMA;
}


void stub_main(void)
{
    for (uint32_t i = 0; i < HDR_WORDS; i++)
        HDR[i] = 0;
    HDR[0] = 0x4E465701u;
    HDR[1] = PROBE_MODE;
    HDR[2] = PROBE_BLOCK;
    HDR[3] = 0xDEADu;

    ROM_NF_BOOT_HW_INIT();
    REG32(NF_TIM_A) = PROBE_TIM_A;
    REG32(NF_TIM_B) = PROBE_TIM_B;
    REG32(NF_DMA) = 0x00010000u;

    const uint32_t row = PROBE_BLOCK * PROBE_PPB + PROBE_PAGE_IN_BLOCK;
    HDR[4] = row;

    uint32_t status = 0;
    if (!read_status(&status)) {
        HDR[3] = ST_ERR_SEQ_STATUS;
        return;
    }
    HDR[5] = status;

    if (!(status & ST_NOT_WP)) {
        HDR[3] = ST_ERR_WP;
        return;
    }

#if PROBE_MODE == 0
    HDR[3] = ST_OK;
    return;
#else
    uint32_t erase_st = 0;
    if (!erase_block(PROBE_BLOCK * PROBE_PPB, &erase_st)) {
        HDR[3] = ST_ERR_SEQ_ERASE;
        return;
    }
    HDR[6] = erase_st;
    if (erase_st & ST_FAIL) {
        HDR[3] = ST_ERR_ERASE_FAIL;
        return;
    }

    int rc = read_raw(row, 0, SCRATCH, 512u);
    if (rc) {
        HDR[3] = (uint32_t)rc;
        return;
    }
    for (uint32_t i = 0; i < 512u; i++) {
        if (SCRATCH[i] != 0xFFu) {
            HDR[3] = ST_ERR_NOT_BLANK;
            HDR[7] = i;
            HDR[8] = SCRATCH[i];
            return;
        }
    }

#if PROBE_MODE == 1
    HDR[3] = ST_OK;
    return;
#else
    uint32_t prog_st = 0;
    if (!program_page(row, &prog_st)) {
        HDR[3] = ST_ERR_SEQ_PROG;
        return;
    }
    HDR[9] = prog_st;
    HDR[10] = REG32(NF_DMA);
    HDR[11] = REG32(L2_STAT);
    if (prog_st & ST_FAIL) {
        HDR[3] = ST_ERR_PROG_FAIL;
        return;
    }

    uint32_t mismatches = 0;
    uint32_t first_bad = 0xFFFFFFFFu;
    const volatile uint32_t *got = (const volatile uint32_t *)SCRATCH;
    for (uint32_t c = 0; c < CHUNKS; c++) {
        rc = read_raw(row, c * 512u, SCRATCH, 512u);
        if (rc) {
            HDR[3] = (uint32_t)rc;
            return;
        }
        for (uint32_t w = 0; w < 128u; w++) {
            uint32_t i = c * 128u + w;
            if (got[w] != pattern_word(i)) {
                if (first_bad == 0xFFFFFFFFu) {
                    first_bad = i;
                    HDR[13] = got[w];
                    HDR[14] = pattern_word(i);
                }
                mismatches++;
            }
        }
        if (c == 0)
            for (uint32_t w = 0; w < 4u; w++)
                HDR[16 + w] = got[w];
    }
    HDR[12] = first_bad;
    HDR[15] = mismatches;
    HDR[3] = mismatches ? ST_ERR_VERIFY : ST_OK;
    return;
#endif
#endif
}
