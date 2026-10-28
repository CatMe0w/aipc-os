#include <stdint.h>
#include "sd.h"
#include "mmio.h"

#define SYSCTRL(off)  REG32(0x08000000u + (off))
#define MCI(off)      REG32(0x20020000u + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_RESP0     MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALEN   MCI(0x28)
#define MCI_DATACNT   MCI(0x30)
#define MCI_DATACTRL  MCI(0x2C)
#define MCI_STA       MCI(0x34)
#define MCI_MASK      MCI(0x38)
#define MCI_DMACTRL   MCI(0x3C)
#define MCI_FIFO      MCI(0x40)

#define MCI_ENABLE        (1u << 20)
#define MCI_FAIL          (1u << 19)
#define MCI_CLK_EN        (1u << 16)
#define MCI_PWRSAVE       (1u << 17)

#define CPSM_ENABLE       (1u << 0)
#define CPSM_RESPONSE     (1u << 7)
#define CPSM_LONGRSP      (1u << 8)
#define CPSM_RSPCRC_NOCHK (1u << 10)
#define CPSM_WITHDATA     (1u << 11)

#define DPSM_ENABLE       (1u << 0)
#define DPSM_DIR_READ     (1u << 1)
#define DPSM_BUS_4BIT     (1u << 3)   /* DATACTRL busmode field = 4-bit */
#define DPSM_BLKSZ_512    (512u << 16)

#define STA_RESPCRCFAIL   (1u << 0)
#define STA_RESPTIMEO     (1u << 2)
#define STA_RESPEND       (1u << 4)
#define STA_CMDSENT       (1u << 5)
#define STA_DATACRCFAIL   (1u << 1)
#define STA_DATATIMEO     (1u << 3)
#define STA_DATAEND       (1u << 6)
#define STA_DATABLOCKEND  (1u << 7)
#define STA_STARTBITERR   (1u << 8)
#define STA_RXACTIVE      (1u << 11)
#define STA_FIFOFULL      (1u << 12)
#define STA_DATA_ERROR    (STA_DATACRCFAIL | STA_DATATIMEO | STA_STARTBITERR)

#define RESP_MASK         (STA_RESPEND | STA_RESPTIMEO | STA_RESPCRCFAIL)

/* R1 card status */
#define R1_ERRORS         0xFDF90008u
#define R1_READY_FOR_DATA (1u << 8)
#define R1_STATE(r)       (((r) >> 9) & 0xFu)
#define R1_STATE_TRAN     4u

#define L2_CONBUF0_7      REG32(0x2002C088u)
#define L2_BUFASSIGN1     REG32(0x2002C090u)
#define L2_BUFSTAT1       REG32(0x2002C0A0u)
#define L2_DMAREQ         REG32(0x2002C080u)
#define  L2_DMA_ENABLE    (1u << 0)
#define  L2_FRACTION      (1u << 9)
#define  L2_REQ_MASK      0xFFFF0000u

/* Buffer 4 for MCI, buffer 5 for NFC, so the fallback path has no contention. */
#define SD_L2_BUF_ID      4u
#define SD_L2_BUF_ADDR    (0x48000000u + SD_L2_BUF_ID * 512u)
/* L2 common buffer config [15:8] is the per-buffer direction. Reads expect
 * the bit clear, so a write sets it and clears it again. */
#define SD_L2_DIR_WRITE   (1u << (SD_L2_BUF_ID + 8))

#define MCI_DMACTRL_L2    0x01000001u  /* BUFEN + 128 words at bit 17 */

/* L2 DMA between DDR and the SD buffer. The count is in 64-byte operations.
 * An operation count above 128 stalls after the first block, so a request
 * moves in chunks of 8 KiB. */
#define L2_DMA_ADDR       REG32(0x2002C000u + SD_L2_BUF_ID * 4u)
#define L2_DMA_COUNT      REG32(0x2002C040u + SD_L2_BUF_ID * 4u)
#define L2_DMA_REQ        (1u << (24u + SD_L2_BUF_ID))
#define L2_DMA_CHUNK      8192u

/* About 100 ms at 20.67 MHz. All ones would never time out, and this driver
 * has no other timeout. */
#define MCI_DATA_TIMEOUT  0x00200000u

static int      g_sdhc;
static int      g_bus_4bit;  /* set once ACMD6 puts the card in 4-bit mode */
static uint32_t g_rca_arg;   /* RCA already shifted into CMD7 arg form */

/* sd_init() captures these at entry and sd_release_pins() restores them.
 * start.S must clear 0x0C before this point, or the fallback restores the
 * bootrom clock gating. */
static struct {
    uint32_t sc_0c, sc_74, sc_78, sc_a0;
    int valid;
} g_pin_save;

static void small_delay(void)
{
    for (volatile uint32_t i = 0; i < 2000u; i++)
        __asm__ volatile ("" : : : "memory");
}

#define WAIT_LIMIT 500000u
static uint32_t wait_sta(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t sta = MCI_STA;
        if (sta & mask)
            return sta;
    }
    return 0;
}

static void sd_stop_transfer(void)
{
    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DATALEN  = 0;
    MCI_DMACTRL  = 0;
    MCI_MASK     = 0;
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));
}

static void sd_quiesce(void)
{
    sd_stop_transfer();
    L2_BUFASSIGN1 &= ~(0x7u << 12);
}

static int send_resp(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                     uint32_t *resp_out)
{
    if (MCI_CMD & CPSM_ENABLE) {
        MCI_CMD = 0;
        small_delay();
    }
    MCI_ARG = arg;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | flags | (cmd_idx << 1);

    uint32_t sta = wait_sta(RESP_MASK);
    /* CRCFAIL counts as a response. The caller wants RESP0 in either case. */
    if (!(sta & (STA_RESPEND | STA_RESPCRCFAIL)))
        return -1;
    if (resp_out)
        *resp_out = MCI_RESP0;
    return 0;
}

/* A write to the divider field also clears CLK_EN, so restore it. */
static void sd_set_clock_divider(uint32_t divider)
{
    uint32_t cur = MCI_CLOCK;
    int was_clk_en = (cur & MCI_CLK_EN) != 0;
    uint32_t low = divider - (divider >> 1);
    uint32_t high = divider >> 1;
    MCI_CLOCK = (cur & 0xFFFF0000u & ~MCI_PWRSAVE) | (high << 8) | low;
    if (was_clk_en)
        MCI_CLOCK |= MCI_CLK_EN;
    small_delay();
}

static void sd_bind_l2(void)
{
    L2_CONBUF0_7 |= (1u << SD_L2_BUF_ID) | (1u << (SD_L2_BUF_ID + 16));
    /* BUFASSIGN1 holds 3 bits per device. MMC/SD is device index 4. */
    L2_BUFASSIGN1 = (L2_BUFASSIGN1 & ~(0x7u << 12)) | (SD_L2_BUF_ID << 12);
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));
}

int sd_init(void)
{
    g_sdhc = 0;
    g_bus_4bit = 0;
    g_rca_arg = 0;

    /* A second sd_init() before sd_release_pins() must keep the values from
     * the first, or the release would restore the SD setup. */
    if (!g_pin_save.valid) {
        g_pin_save.sc_0c = SYSCTRL(0x0C);
        g_pin_save.sc_74 = SYSCTRL(0x74);
        g_pin_save.sc_78 = SYSCTRL(0x78);
        g_pin_save.sc_a0 = SYSCTRL(0xA0);
        g_pin_save.valid = 1;
    }

    /* Clock-gate toggle: clears sticky MCI state from a prior attempt. */
    SYSCTRL(0x0C) |=  (1u << 2);
    small_delay();
    SYSCTRL(0x0C) &= ~(1u << 2);
    small_delay();

    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    small_delay();

    SYSCTRL(0xA0) |= 0x180u;
    small_delay();

    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | MCI_PWRSAVE | 0xF0u;
    small_delay();

    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    MCI_CMD       = 0;
    MCI_DATACTRL  = 0;
    MCI_DMACTRL   = 0;
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_MASK      = 0;

    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    wait_sta(STA_CMDSENT | STA_RESPTIMEO);
    small_delay();

    uint32_t resp8 = 0;
    int ok8 = 0;
    for (uint32_t r = 0; r < 10u; r++) {
        if (r) small_delay();
        if (send_resp(8, 0x1AAu, CPSM_RESPONSE, &resp8) == 0 && resp8 == 0x1AAu) {
            ok8 = 1;
            break;
        }
    }
    if (!ok8)
        return -8;

    uint32_t ocr_arg = 0x40FF8000u;
    uint32_t ocr = 0;
    int ready = 0;
    for (uint32_t a = 0; a < 100u; a++) {
        uint32_t tmp;
        if (send_resp(55, 0, CPSM_RESPONSE, &tmp) < 0)
            break;
        if (send_resp(41, ocr_arg, CPSM_RESPONSE | CPSM_RSPCRC_NOCHK, &ocr) < 0)
            break;
        if (ocr & 0x80000000u) { ready = 1; break; }
        if (a == 0 && (ocr & 0x00FFFFFFu))
            ocr_arg = (ocr & 0x40FF8000u) | 0x40FF8000u;
        small_delay();
    }
    if (!ready)
        return -41;
    g_sdhc = (ocr & 0x40000000u) ? 1 : 0;

    MCI_ARG = 0;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | CPSM_LONGRSP | (2u << 1);
    if (!(wait_sta(RESP_MASK) & STA_RESPEND))
        return -2;

    uint32_t r6;
    if (send_resp(3, 0, CPSM_RESPONSE, &r6) < 0)
        return -3;
    g_rca_arg = r6 & 0xFFFF0000u;

    if (send_resp(7, g_rca_arg, CPSM_RESPONSE, 0) < 0)
        return -7;

    small_delay();

    {
        uint32_t tmp;
        if (send_resp(55, g_rca_arg, CPSM_RESPONSE, &tmp) >= 0 &&
            send_resp(6, 0x2u, CPSM_RESPONSE, &tmp) >= 0)
            g_bus_4bit = 1;
    }

    sd_set_clock_divider(0x04u);
    sd_bind_l2();
    return 0;
}

void sd_release_pins(void)
{
    sd_quiesce();

    if (!g_pin_save.valid)
        return;
    SYSCTRL(0x0C) = g_pin_save.sc_0c;
    SYSCTRL(0x74) = g_pin_save.sc_74;
    SYSCTRL(0x78) = g_pin_save.sc_78;
    SYSCTRL(0xA0) = g_pin_save.sc_a0;
    g_pin_save.valid = 0;
}

static int sd_read_block_once(uint32_t lba, void *dst)
{
    uint32_t arg = g_sdhc ? lba : (lba << 9);

    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL  = 0;

    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));

    MCI_DMACTRL   = MCI_DMACTRL_L2;
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = 512u;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);

    MCI_ARG  = arg;
    MCI_MASK = 0x1FFu;
    MCI_CMD  = CPSM_ENABLE | CPSM_RESPONSE | (17u << 1);

    if (!(wait_sta(RESP_MASK) & STA_RESPEND)) {
        sd_stop_transfer();
        return -1;
    }
    (void)MCI_RESP0;  /* clear sticky RESPEND */

    uint32_t spins = 0;
    while (((L2_BUFSTAT1 >> (SD_L2_BUF_ID * 4)) & 0xFu) < 8u) {
        if (++spins >= WAIT_LIMIT) {
            sd_stop_transfer();
            return -2;
        }
    }

    const volatile uint32_t *src = MMIO_PTR(SD_L2_BUF_ADDR);
    uint32_t *dw = (uint32_t *)dst;
    for (uint32_t w = 0; w < 128u; w++)
        dw[w] = src[w];

    sd_stop_transfer();
    return 0;
}

/* The first transfer after sd_init() always fails once. See
 * docs/nk/sdhc-driver.md#first-transfer-after-initialization. */
#define SD_READ_TRIES 5u

int sd_read_block(uint32_t lba, void *dst)
{
    int rc = -1;

    for (uint32_t try = 0; try < SD_READ_TRIES; try++) {
        rc = sd_read_block_once(lba, dst);
        if (rc == 0)
            return 0;
    }
    return rc;
}

/* The card programs the block after the data phase. Polls CMD13 until it is
 * back in the transfer state, up to about 1 s. */
static int sd_wait_ready(void)
{
    for (uint32_t i = 0; i < 40000u; i++) {
        uint32_t r1;

        if (send_resp(13, g_rca_arg, CPSM_RESPONSE, &r1) == 0 &&
            (r1 & R1_READY_FOR_DATA) && R1_STATE(r1) == R1_STATE_TRAN)
            return 0;
        small_delay();
    }
    return -13;
}

static int sd_write_block_once(uint32_t lba, const void *src)
{
    uint32_t arg = g_sdhc ? lba : (lba << 9);
    uint32_t r1, sta, seen = 0;
    int rc = 0;

    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL  = 0;

    if (send_resp(24, arg, CPSM_RESPONSE | CPSM_WITHDATA, &r1) < 0 ||
        (r1 & R1_ERRORS)) {
        sd_stop_transfer();
        return -24;
    }

    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));
    L2_CONBUF0_7 |= SD_L2_DIR_WRITE;
    volatile uint32_t *dst = MMIO_PTR(SD_L2_BUF_ADDR);
    const uint32_t *sw = (const uint32_t *)src;
    for (uint32_t w = 0; w < 128u; w++)
        dst[w] = sw[w];

    MCI_DMACTRL   = MCI_DMACTRL_L2;
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = 512u;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_BLKSZ_512
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);

    for (uint32_t i = 0;; i++) {
        sta = MCI_STA;
        seen |= sta;
        if (sta & STA_DATA_ERROR) {
            rc = -2;
            break;
        }
        if ((seen & (STA_DATAEND | STA_DATABLOCKEND)) ==
            (STA_DATAEND | STA_DATABLOCKEND))
            break;
        if (i >= WAIT_LIMIT) {
            rc = -3;
            break;
        }
    }

    sd_stop_transfer();
    L2_CONBUF0_7 &= ~SD_L2_DIR_WRITE;
    if (rc) {
        /* Ends the receive state that a failed data phase can leave. */
        (void)send_resp(12, 0, CPSM_RESPONSE, 0);
    }
    if (sd_wait_ready())
        return rc ? rc : -4;
    return rc;
}

#define SD_WRITE_TRIES 3u

int sd_write_block(uint32_t lba, const void *src)
{
    int rc = -1;

    for (uint32_t try = 0; try < SD_WRITE_TRIES; try++) {
        rc = sd_write_block_once(lba, src);
        if (rc == 0)
            return 0;
    }
    return rc;
}

static void l2_dma_chunk(uint32_t addr, uint32_t bytes)
{
    L2_DMA_ADDR  = addr;
    L2_DMA_COUNT = bytes / 64u;
    L2_DMAREQ = (L2_DMAREQ & ~(L2_FRACTION | L2_REQ_MASK)) | L2_DMA_ENABLE |
                L2_DMA_REQ;
}

/* Polls for one chunk. MCI_DATA_TIMEOUT ends a transfer that stalls on the
 * card side well before this limit. */
#define DMA_WAIT_LIMIT 20000000u

int sd_read_blocks(uint32_t lba, uint32_t n, void *dst)
{
    uint32_t addr = (uint32_t)(uintptr_t)dst;
    uint32_t total = n * 512u, sent, r1, seen = 0;
    uint32_t enable = L2_DMAREQ & L2_DMA_ENABLE;
    int rc = 0;

    if (n == 0 || n > SD_BLOCKS_MAX || (addr & 63u))
        return -1;
    if (send_resp(23, n, CPSM_RESPONSE, &r1) < 0 || (r1 & R1_ERRORS))
        return SD_ENOCMD23;

    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL  = 0;
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));

    sent = total < L2_DMA_CHUNK ? total : L2_DMA_CHUNK;
    l2_dma_chunk(addr, sent);

    MCI_DMACTRL   = MCI_DMACTRL_L2;
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = total;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);
    MCI_ARG  = g_sdhc ? lba : (lba << 9);
    MCI_MASK = 0x1FFu;
    MCI_CMD  = CPSM_ENABLE | CPSM_RESPONSE | CPSM_WITHDATA | (18u << 1);

    if (!(wait_sta(RESP_MASK) & STA_RESPEND) || (MCI_RESP0 & R1_ERRORS)) {
        rc = -18;
        goto out;
    }
    for (uint32_t i = 0;; i++) {
        uint32_t sta = MCI_STA;

        seen |= sta;
        if (sta & STA_DATA_ERROR) {
            rc = -20;
            break;
        }
        if (!(L2_DMAREQ & L2_DMA_REQ)) {
            if (sent < total) {
                uint32_t b = total - sent < L2_DMA_CHUNK ? total - sent
                                                         : L2_DMA_CHUNK;

                l2_dma_chunk(addr + sent, b);
                sent += b;
                i = 0;
                continue;
            }
            if (seen & STA_DATAEND) {
                rc = MCI_DATACNT ? -21 : 0;
                break;
            }
        }
        if (i >= DMA_WAIT_LIMIT) {
            rc = -22;
            break;
        }
    }

out:
    sd_stop_transfer();
    if (L2_DMAREQ & L2_DMA_REQ)
        L2_DMA_COUNT = 0;
    L2_DMAREQ = (L2_DMAREQ & ~(L2_FRACTION | L2_REQ_MASK | L2_DMA_ENABLE)) |
                enable;
    /* CMD23 ends a complete transfer. One that failed needs CMD12. */
    if (rc)
        (void)send_resp(12, 0, CPSM_RESPONSE, 0);
    return rc;
}

/* CMD6 in set mode for function 1 of group 1, high speed. The 64-byte switch
 * status comes through the inner FIFO, because it is shorter than a block. */
static int switch_high_speed(void)
{
    uint32_t status[16], r1, sta, seen = 0;

    sd_stop_transfer();
    MCI_DATATIMER = MCI_DATA_TIMEOUT;
    MCI_DATALEN   = 64u;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_DIR_READ | (64u << 16)
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);
    if (send_resp(6, 0x80FFFFF1u, CPSM_RESPONSE | CPSM_WITHDATA, &r1) < 0 ||
        (r1 & R1_ERRORS)) {
        sd_stop_transfer();
        return -6;
    }
    for (uint32_t w = 0; w < 16u; w++) {
        uint32_t i = 0;

        for (;;) {
            sta = MCI_STA;
            seen |= sta;
            if ((sta & STA_DATA_ERROR) || ++i >= WAIT_LIMIT) {
                sd_stop_transfer();
                return -62;
            }
            if ((sta & (STA_FIFOFULL | STA_RXACTIVE)) ==
                (STA_FIFOFULL | STA_RXACTIVE))
                break;
        }
        status[w] = MCI_FIFO;
    }
    for (uint32_t i = 0; !(seen & STA_DATAEND) && i < WAIT_LIMIT; i++)
        seen |= MCI_STA;
    sd_stop_transfer();
    /* Byte 16 holds the function that group 1 selected. */
    return (status[4] & 0xFu) == 1u ? 0 : -64;
}

/* Like any data transfer, CMD6 can be the first one after sd_init(), which
 * always fails once. */
#define SD_SWITCH_TRIES 3u

int sd_high_speed(void)
{
    int rc = -1;

    for (uint32_t try = 0; try < SD_SWITCH_TRIES && rc; try++)
        rc = switch_high_speed();
    if (rc)
        return rc;
    /* MCI_CLOCK 0x0101: 31 MHz from the 124 MHz input */
    sd_set_clock_divider(0x02u);
    return sd_wait_ready();
}

/* SD_CD# is GPIO13, low while a card is in the slot. */
#define GPIO1_DIR         SYSCTRL(0x7C)
#define GPIO1_IN          SYSCTRL(0xBC)
#define SD_CD_BIT         (1u << 13)

int sd_card_present(void)
{
    GPIO1_DIR |= SD_CD_BIT;
    return (GPIO1_IN & SD_CD_BIT) == 0;
}
