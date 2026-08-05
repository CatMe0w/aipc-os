/* Register writes, sequencer words and poll order are verbatim from nboot;
   changing them invalidates the experiment. */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define NF_SEQ   0x2002A100u /* second sequencer block, the one nboot uses */
#define NF_CTRL  0x2002A158u
#define NF_TIM_A 0x2002A15Cu
#define NF_TIM_B 0x2002A160u
#define NF_DMA   0x2002B000u

#define L2_DMA_PATH 0x2002C084u
#define L2_BUF_CFG  0x2002C088u
#define L2_ASSIGN   0x2002C090u
#define L2_STAT     0x2002C0A0u
#define L2_BUF5     0x48000A00u /* buffer index 5; buffers 0-7 are 512 B each */

#define HDR  ((volatile uint32_t *)(uintptr_t)0x48000600u)
#define HDR_WORDS 32u
#define DATA ((volatile uint8_t *)(uintptr_t)0x48000C00u)
#define KEEP 128u
#define SCRATCH ((volatile uint8_t *)(uintptr_t)0x48000700u)
#define REGS ((volatile uint32_t *)(uintptr_t)0x48000680u) /* just past the 32-word header */
#define ECC_REGS 8u

#define COL_CYCLES 2u
#define ROW_CYCLES 3u

#define ROM_NF_BOOT_HW_INIT ((void (*)(void))0x2648u)

#define SPIN 0x400000u

#ifndef PROBE_MODE
#define PROBE_MODE 0
#endif
#ifndef PROBE_PAGE
#define PROBE_PAGE 128u
#endif
#ifndef PROBE_ITERS
#define PROBE_ITERS 400u
#endif
#ifndef PROBE_TIM_A
#define PROBE_TIM_A 0x00030230u
#endif
#ifndef PROBE_TIM_B
#define PROBE_TIM_B 0x00040203u
#endif

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

static void l2_bind_nf_dma(void)
{
    REG32(L2_DMA_PATH) |= 0x30000000u;
    REG32(L2_BUF_CFG) |= 0x00200000u;
    REG32(L2_ASSIGN) = (REG32(L2_ASSIGN) & ~0xE00u) | 0xA00u;
    REG32(L2_BUF_CFG) |= 0x20000000u;
}

static volatile uint32_t *emit_addr(volatile uint32_t *slot, uint32_t row, uint32_t col)
{
    for (uint32_t i = 0; i < COL_CYCLES; i++)
        *slot++ = (((col >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    for (uint32_t i = 0; i < ROW_CYCLES; i++)
        *slot++ = (((row >> (8u * i)) & 0xFFu) << 11) | 0x62u;
    return slot;
}

static uint32_t sum32(const volatile uint8_t *p, uint32_t len)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < len; i++)
        s = (s << 1) + (s >> 31) + p[i];
    return s;
}

#if PROBE_MODE < 2
/* Word stores only: STRB into L2 SRAM splatters the byte across the word. */
static void record(uint32_t slot_index)
{
    volatile uint32_t *d = (volatile uint32_t *)(DATA + slot_index * KEEP);
    const volatile uint32_t *s = (const volatile uint32_t *)SCRATCH;

    HDR[12 + slot_index] = sum32(SCRATCH, 512u);
    for (uint32_t i = 0; i < KEEP / 4u; i++)
        d[i] = s[i];
}
#endif

static int l2_copy_from_buf0(volatile uint8_t *dst, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < SPIN; i++)
        if (((REG32(L2_STAT) >> 20) & 0xFu) >= (len >> 6))
            break;
    if (i == SPIN)
        return 0;

    volatile uint32_t *d = (volatile uint32_t *)dst;
    const volatile uint32_t *s = (const volatile uint32_t *)(uintptr_t)L2_BUF5;
    for (uint32_t w = 0; w < (len >> 2); w++)
        d[w] = s[w];
    return 1;
}

#if PROBE_MODE == 0
static int nf_read_raw_range(uint32_t row, uint32_t col, volatile uint8_t *dst, uint32_t len)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = 0x64u;
    slot = emit_addr(slot, row, col);
    *slot++ = 0x18464u;
    *slot = 0x201u;
    seq_start();
    l2_bind_nf_dma();
    if (!seq_wait())
        return 1;

    REG32(NF_DMA) = (len << 7) | 0x100018u;
    REG32(NF_CTRL) = 0;
    REG32(NF_SEQ) = ((len - 1u) << 11) | 0x119u;
    seq_start();
    if (!seq_wait())
        return 2;
    if (!l2_copy_from_buf0(dst, len))
        return 3;

    for (uint32_t i = 0; i < SPIN; i++) {
        if (REG32(NF_DMA) & 0x40u) {
            REG32(NF_DMA) |= 0x40u;
            return 0;
        }
    }
    return 4;
}
#elif PROBE_MODE == 1
static int nf_read_page_with_ecc(uint32_t row, uint32_t chunks)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = 0x64u;
    slot = emit_addr(slot, row, 0);
    *slot++ = 0x18064u;
    *slot = 0x201u;
    seq_start();
    if (!seq_wait())
        return 1;

    for (uint32_t c = 0; c < chunks; c++) {
        uint32_t status = 0;
        uint32_t i;

        l2_bind_nf_dma();
        REG32(NF_DMA) = 0x0C11049Au;
        REG32(NF_CTRL) = 0;
        REG32(NF_SEQ) = 0x00107919u; /* transfer 528 bytes */
        seq_start();
        if (!l2_copy_from_buf0(SCRATCH, 512u))
            return 0x10u + c;
        if (!seq_wait())
            return 0x20u + c;
        record(c);

        for (i = 0; i < SPIN; i++) {
            status = REG32(NF_DMA);
            if ((status & 0x40u) && (status & 0x01000000u))
                break;
        }
        if (i == SPIN)
            return 0x30u + c;
        REG32(NF_DMA) = status | 0x40u;
        HDR[8 + c] = status;
    }
    return 0;
}
#else
static int nf_read_page_ecc_snapshot(uint32_t row, uint32_t chunks)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = 0x64u;
    slot = emit_addr(slot, row, 0);
    *slot++ = 0x18064u;
    *slot = 0x201u;
    seq_start();
    if (!seq_wait())
        return 1;

    for (uint32_t c = 0; c < chunks; c++) {
        uint32_t status = 0;
        uint32_t i;

        l2_bind_nf_dma();
        REG32(NF_DMA) = 0x0C11049Au;
        REG32(NF_CTRL) = 0;
        REG32(NF_SEQ) = 0x00107919u;
        seq_start();
        if (!l2_copy_from_buf0(SCRATCH, 512u))
            return 0x10u + c;
        if (!seq_wait())
            return 0x20u + c;

        for (i = 0; i < SPIN; i++) {
            status = REG32(NF_DMA);
            if ((status & 0x40u) && (status & 0x01000000u))
                break;
        }
        if (i == SPIN)
            return 0x30u + c;

        /* Snapshot the whole ECC block before END is cleared. */
        for (uint32_t k = 0; k < ECC_REGS; k++)
            REGS[c * ECC_REGS + k] = REG32(0x2002B000u + 4u * k);

        HDR[16 + c] = (status & 0x4000000u) ? 1u : ((status & 0x8000000u) ? 3u : 2u);
        HDR[20 + c] = sum32(SCRATCH, 512u);
        HDR[24 + c] = status;
        REG32(NF_DMA) = status | 0x40u;
#if PROBE_MODE == 3
        if (HDR[16 + c] != 1u)
            return 0x100u + c;
#endif
    }
    return 0;
}
#endif

void stub_main(void)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;
    uint32_t saved_assign;

    for (uint32_t i = 0; i < HDR_WORDS; i++)
        HDR[i] = 0;
    for (uint32_t i = 0; i < KEEP; i++) /* 4 blocks of KEEP bytes, in words */
        ((volatile uint32_t *)DATA)[i] = 0;
    for (uint32_t i = 0; i < 4u * ECC_REGS; i++)
        REGS[i] = 0;

    HDR[0] = 0x4E464301u;
    HDR[1] = PROBE_MODE;
    HDR[2] = PROBE_PAGE;
    HDR[3] = 0xDEADu;

    saved_assign = REG32(L2_ASSIGN);
    REG32(L2_ASSIGN) &= 0xFFFFFFC0u;

    ROM_NF_BOOT_HW_INIT();
    REG32(NF_TIM_A) = PROBE_TIM_A;
    REG32(NF_TIM_B) = PROBE_TIM_B;
    REG32(NF_DMA) = 0x00010000u;

    REG32(NF_CTRL) = 0;
    slot[0] = 0x0007F864u;
    slot[1] = 0x00032401u;
    seq_start();
    if (!seq_wait()) {
        HDR[3] = 0xFFu;
        REG32(L2_ASSIGN) = saved_assign;
        return;
    }

#if PROBE_MODE == 0
    HDR[4] = 0x000u;
    HDR[5] = 0x200u;
    HDR[6] = 0x400u;
    for (uint32_t i = 0; i < 3; i++) {
        int rc = nf_read_raw_range(PROBE_PAGE, HDR[4 + i], SCRATCH, 512u);
        if (rc) {
            HDR[3] = (uint32_t)rc | (i << 8);
            REG32(L2_ASSIGN) = saved_assign;
            return;
        }
        record(i);
        HDR[8 + i] = REG32(NF_DMA);
    }
#elif PROBE_MODE == 2
    {
        int rc = nf_read_page_ecc_snapshot(PROBE_PAGE, 4u);
        if (rc) {
            HDR[3] = (uint32_t)rc;
            REG32(L2_ASSIGN) = saved_assign;
            return;
        }
    }
#elif PROBE_MODE == 3
    for (uint32_t it = 0; it < PROBE_ITERS; it++) {
        int rc = nf_read_page_ecc_snapshot(PROBE_PAGE, 4u);
        HDR[28] = it;
        if (rc) {
            HDR[3] = (uint32_t)rc;
            REG32(L2_ASSIGN) = saved_assign;
            return;
        }
    }
#else
    {
        int rc = nf_read_page_with_ecc(PROBE_PAGE, 4u);
        if (rc) {
            HDR[3] = (uint32_t)rc;
            REG32(L2_ASSIGN) = saved_assign;
            return;
        }
    }
#endif

    HDR[7] = REG32(NF_CTRL);
    HDR[3] = 0;
    REG32(L2_ASSIGN) = saved_assign;
}
