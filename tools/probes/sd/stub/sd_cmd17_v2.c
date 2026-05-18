/*
 * sd_cmd17_v2 - identical init to sd_acmd41_loop, then CMD17
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20020000u
#define MCI(off)      REG32(MCI_BASE + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_STA       MCI(0x34)
#define MCI_MASK      MCI(0x38)
#define MCI_RESP0     MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALEN   MCI(0x28)
#define MCI_DATACTRL  MCI(0x2C)
#define MCI_FIFO      MCI(0x40)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)

#define CLK_MMC_BIT   (1u << 2)

#define STA_RESP_END    (1u << 4)
#define STA_RESP_TIMEO  (1u << 2)
#define STA_RESP_CRC    (1u << 0)
#define STA_CMD_SENT    (1u << 5)
#define STA_DATA_END    (1u << 6)
#define STA_DATA_BLKEND (1u << 7)
#define STA_FIFO_EMPTY  (1u << 13)

#define DPSM_ENABLE     (1u << 0)
#define DPSM_DIR_READ   (1u << 1)
#define DPSM_BLKSZ_512  (512u << 16)
#define DPSM_BUS_1BIT   (0u << 3)

#define RESP_MASK (STA_RESP_END | STA_RESP_TIMEO | STA_RESP_CRC)
#define DATA_MASK (STA_DATA_END | STA_DATA_BLKEND | (1u<<3) | (1u<<1))

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void small_delay(void)
{
    for (volatile uint32_t i = 0; i < 2000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void micro_delay(void)
{
    for (volatile uint32_t i = 0; i < 200u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

#define WAIT_LIMIT 300000u
static uint32_t wait_sta_capture(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t sta = MCI_STA;
        if (sta & mask)
            return ((i + 1u) << 16) | (sta & 0xFFFFu);
    }
    return 0;
}

static int send_resp(uint32_t cmd_idx, uint32_t arg, uint32_t *resp_out, uint32_t *sta_out)
{
    MCI_ARG = arg;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (cmd_idx << 1);

    uint32_t r = wait_sta_capture(RESP_MASK);
    uint32_t sta = r & 0xFFFFu;
    *sta_out = sta;

    if (sta & (STA_RESP_END | STA_RESP_CRC)) {
        *resp_out = MCI_RESP0;
        return 0;
    }
    return -1;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x43313732u; /* "C172" */
    OUT[1] = 2u;

    /* === EXACT copy of sd_acmd41_loop init === */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT; small_delay();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT; small_delay();

    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    micro_delay();

    SYSCTRL(0x9C) |= 0x180u;
    SYSCTRL(0xA0) |= 0x180u;
    SYSCTRL(0xA4) |= 0x180u;
    micro_delay();

    MCI_CLOCK = 0; small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL; small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    small_delay();
    MCI_DATATIMER = 0x00030000u;
    MCI_MASK = 0xFFFFFFFFu;
    MCI_CMD = 0;
    MCI_DATACTRL = 0;
    MCI(0x3C) = 0;  /* MCI_DMACTRL = 0 (internal FIFO mode) */

    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    /* CMD0 */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    small_delay();

    /* CMD8 */
    uint32_t resp8, sta8;
    if (send_resp(8, 0x1AAu, &resp8, &sta8) < 0 || resp8 != 0x1AAu) {
        OUT[2] = 0xBAD8u;
        OUT[3] = sta8;
        return;
    }
    OUT[2] = resp8;
    OUT[3] = sta8;

    /* ACMD41 loop */
    uint32_t ocr_arg = 0x40000000u, ocr = 0, sta;
    for (uint32_t a = 0; a < 200; a++) {
        if (send_resp(55, 0, &resp8, &sta) < 0) { OUT[4]=0xBAD55u|a; return; }
        if (send_resp(41, ocr_arg, &ocr, &sta) < 0) { OUT[4]=0xBAD41u|a; return; }
        if (ocr & 0x80000000u) break;
        if (a == 0 && (ocr & 0x00FFFFFFu)) ocr_arg = ocr & 0x40FF8000u;
        small_delay();
    }
    if (!(ocr & 0x80000000u)) { OUT[4]=0xBAD41FFu; return; }

    OUT[4] = ocr;
    int sdhc = (ocr & 0x40000000u) ? 1 : 0;
    OUT[5] = sdhc;

    /* CMD2: ALL_SEND_CID (long response, R2 = 136 bits)
     * Card goes: ready -> identification */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (1u << 8) | (2u << 1);  /* LONGRSP bit8 */
    uint32_t r2 = wait_sta_capture(RESP_MASK);
    OUT[42] = r2;
    if (!((r2 & 0xFFFFu) & STA_RESP_END)) { OUT[43]=0xBAD2u; return; }
    small_delay();

    /* CMD3: SEND_RELATIVE_ADDR (R6 response)
     * Card goes: identification -> stand-by */
    uint32_t rca, st;
    if (send_resp(3, 0, &rca, &st) < 0) { OUT[43]=0xBAD3u; return; }
    OUT[43] = rca;  /* RCA value */
    small_delay();

    /* CMD7: SELECT_CARD with RCA (already in bits [31:16] from R6 response)
     * Card goes: stand-by -> transfer */
    uint32_t sel_arg = rca & 0xFFFF0000u;
    uint32_t sel_resp;
    if (send_resp(7, sel_arg, &sel_resp, &st) < 0) { OUT[44]=0xBAD7u; return; }
    OUT[44] = sel_resp;

    /* Configure L2 buffer as data target. DMACTRL: bit0=DMA_BUFEN, bits[15:1]=L2 word offset */
    #define L2_DATA_BASE 0x48001200u
    uint32_t l2_word_off = (L2_DATA_BASE - 0x48000000u) / 4u;
    MCI(0x3C) = 1u | (l2_word_off << 1);  /* DMA_BUFEN + word offset, no DMA_EN */

    /* Set up data transfer: 1 block of 512 bytes, read direction */
    MCI_DATALEN = 512u;
    MCI_DATACTRL = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512 | DPSM_BUS_1BIT;

    /* CMD17: READ_SINGLE_BLOCK */
    MCI_ARG = sdhc ? 0u : 0u;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (17u << 1);

    /* Wait for command response first */
    uint32_t cr = wait_sta_capture(RESP_MASK);
    OUT[6] = cr;
    if (!((cr & 0xFFFFu) & (STA_RESP_END))) { OUT[7] = 0xBAD17Cu; return; }

    /* Poll for data in L2 buffer: read words from L2 buffer directly */
    volatile uint32_t *l2buf = (volatile uint32_t *)(uintptr_t)L2_DATA_BASE;
    for (int i = 0; i < 128; i++) {
        for (volatile uint32_t w = 0; w < WAIT_LIMIT; w++) {
            if (MCI_STA & ((1u << 14) | (1u << 12)))
                break;
        }
        uint32_t w = l2buf[i];
        if (i < 31) OUT[8 + i] = w;
        else if (i == 127) OUT[39] = w;
    }
    OUT[40] = (OUT[39] >> 16) & 0xFFu;
    OUT[41] = (OUT[39] >> 24) & 0xFFu;

    /* Now check data completion */
    uint32_t dr = MCI_STA;
    OUT[45] = dr;
    OUT[7] = 0;

}
