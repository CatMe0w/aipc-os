#include "nf_common.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define NF_SEQ   0x2002A100u
#define NF_CTRL  0x2002A158u
#define NF_TIM_A 0x2002A15Cu
#define NF_TIM_B 0x2002A160u
#define NF_INFO  0x2002A150u    /* reg20, where a status read lands */
#define NF_DMA   0x2002B000u

#define L2_DMA_PATH 0x2002C084u
#define L2_BUF_CFG  0x2002C088u
#define L2_ASSIGN   0x2002C090u
#define L2_STAT     0x2002C0A0u
#define L2_BUF5     0x48000A00u

#define COL_CYCLES 2u
#define ROW_CYCLES 3u

#define ROM_NF_BOOT_HW_INIT ((void (*)(void))0x2648u)
#define SPIN 0x400000u

#define TIM_A 0x000F5AD1u
#define TIM_B 0x000F5C5Cu

#define ST_FAIL 0x01u

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

static void l2_bind(void)
{
    REG32(L2_DMA_PATH) |= 0x30000000u;
    REG32(L2_BUF_CFG) |= 0x00200000u;
    REG32(L2_ASSIGN) = (REG32(L2_ASSIGN) & ~0xE00u) | 0xA00u;
    REG32(L2_BUF_CFG) |= 0x20000000u;
}

/* L2 common buffer config [15:8] is per-buffer direction; buffer 5 is bit 13.
 * The read path never touches it. Writes must set it and clear it after. */
static void l2_dir_write(int on)
{
    if (on)
        REG32(L2_BUF_CFG) |= (1u << 13);
    else
        REG32(L2_BUF_CFG) &= ~(1u << 13);
}

static int dma_wait(void)
{
    for (uint32_t i = 0; i < SPIN; i++) {
        if (REG32(NF_DMA) & 0x40u) {
            REG32(NF_DMA) |= 0x40u;
            return NF_OK;
        }
    }
    return NF_E_DMA;
}

static volatile uint32_t *emit_addr(volatile uint32_t *slot, uint32_t row,
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
        return NF_E_SEQ;
    *out = REG32(NF_INFO) & 0xFFu;
    return NF_OK;
}

static int check_status(uint32_t *status)
{
    int rc = read_status(status);
    if (rc)
        return rc;
    return (*status & ST_FAIL) ? NF_E_FAIL : NF_OK;
}

/* READ ID, four bytes into reg20. Same micro-op as a status read, with a byte
 * count instead of a single byte. */
int nf_read_id(uint32_t *out)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = (0x90u << 11) | 0x64u;
    *slot++ = 0x62u;                            /* one address byte, 0x00 */
    *slot = ((4u - 1u) << 11) | 0x58u | 0x001u;
    seq_start();
    if (!seq_wait())
        return NF_E_SEQ;
    *out = REG32(NF_INFO);
    return NF_OK;
}

void nf_hw_init(void)
{
    ROM_NF_BOOT_HW_INIT();
    REG32(NF_TIM_A) = TIM_A;
    REG32(NF_TIM_B) = TIM_B;
    REG32(NF_DMA) = 0x00010000u;
}

/* One full READ command sequence per call, so col may be any offset into the
 * 2112-byte physical page. len must be a multiple of 4 and at most NF_CHUNK. */
static int nf_read_raw(uint32_t row, uint32_t col, volatile uint8_t *dst, uint32_t len)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = 0x64u;
    slot = emit_addr(slot, row, col, 1);
    *slot++ = 0x18464u;
    *slot = 0x201u;
    seq_start();
    l2_bind();                  /* after the start, not before */
    if (!seq_wait())
        return NF_E_SEQ;

    REG32(NF_DMA) = (len << 7) | 0x100018u;
    REG32(NF_CTRL) = 0;
    REG32(NF_SEQ) = ((len - 1u) << 11) | 0x119u;
    seq_start();
    if (!seq_wait())
        return NF_E_SEQ;

    uint32_t i;
    for (i = 0; i < SPIN; i++)
        if (((REG32(L2_STAT) >> 20) & 0xFu) >= (len >> 6))
            break;
    if (i == SPIN)
        return NF_E_L2;

    volatile uint32_t *d = (volatile uint32_t *)dst;
    const volatile uint32_t *s = (const volatile uint32_t *)(uintptr_t)L2_BUF5;
    for (uint32_t w = 0; w < (len >> 2); w++)
        d[w] = s[w];

    return dma_wait();
}

int nf_read_page(uint32_t row, volatile uint8_t *dst)
{
    for (uint32_t c = 0; c < NF_PAGE_DATA / NF_CHUNK; c++) {
        int rc = nf_read_raw(row, c * NF_CHUNK, dst + c * NF_CHUNK, NF_CHUNK);
        if (rc)
            return rc;
    }
    return nf_read_raw(row, NF_PAGE_DATA, dst + NF_PAGE_DATA,
                       NF_PAGE_RAW - NF_PAGE_DATA);
}

int nf_erase_block(uint32_t row, uint32_t *status)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;

    REG32(NF_CTRL) = 0;
    *slot++ = (0x60u << 11) | 0x64u;
    slot = emit_addr(slot, row, 0, 0);
    *slot++ = (0xD0u << 11) | 0x64u;
    *slot = 0x201u;
    seq_start();
    if (!seq_wait())
        return NF_E_SEQ;

    return check_status(status);
}

/* Word stores only: STRB into L2 SRAM splatters the byte across the word. */
static int program_chunk(const volatile uint8_t *src)
{
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)L2_BUF5;
    const volatile uint32_t *s = (const volatile uint32_t *)src;

    for (uint32_t w = 0; w < NF_CHUNK / 4u; w++)
        dst[w] = s[w];

    REG32(NF_DMA) = (NF_CHUNK << 7) | 0x10001Cu;    /* read descriptor + DIR_WRITE */
    REG32(NF_CTRL) = 0;
    REG32(NF_SEQ) = ((NF_CHUNK - 1u) << 11) | 0x128u | 0x001u;
    seq_start();
    if (!seq_wait())
        return NF_E_SEQ;

    return dma_wait();
}

/* Programs NF_PAGE_DATA bytes. The remaining 64 physical columns keep the 0xFF
 * the erase left: nothing in the boot chain reads them, and a second 0x80 to
 * reach them would spend an extra NOP, whose limit is unknown for this part. */
static int program_data(uint32_t row, const volatile uint8_t *src)
{
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;
    int rc;

    REG32(NF_CTRL) = 0;
    *slot++ = (0x80u << 11) | 0x64u;
    slot = emit_addr(slot, row, 0, 1);
    slot[-1] |= 0x001u;
    seq_start();
    if (!seq_wait())
        return NF_E_SEQ;

    for (uint32_t c = 0; c < NF_PAGE_DATA / NF_CHUNK; c++) {
        rc = program_chunk(src + c * NF_CHUNK);
        if (rc)
            return rc;
    }

    slot = (volatile uint32_t *)(uintptr_t)NF_SEQ;
    REG32(NF_CTRL) = 0;
    *slot++ = (0x10u << 11) | 0x64u;
    *slot = 0x201u;
    seq_start();
    return seq_wait() ? NF_OK : NF_E_SEQ;
}

int nf_program_page(uint32_t row, const volatile uint8_t *src, uint32_t *status)
{
    int rc;

    l2_bind();
    l2_dir_write(1);
    rc = program_data(row, src);
    l2_dir_write(0);
    if (rc)
        return rc;

    return check_status(status);
}
