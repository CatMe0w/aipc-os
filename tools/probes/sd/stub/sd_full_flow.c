/*
 * sd_full_flow - complete SD init + CMD17 data read
 * Using the working init sequence from sd_noreset_probe
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
#define MCI_RESP1     MCI(0x18)
#define MCI_RESP2     MCI(0x1C)
#define MCI_RESP3     MCI(0x20)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALEN   MCI(0x28)
#define MCI_DATACTRL  MCI(0x2C)
#define MCI_DMACTRL   MCI(0x3C)
#define MCI_FIFO      MCI(0x40)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)
#define MCI_PWRSAVE   (1u << 17)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)
#define CPSM_LONGRSP  (1u << 8)
#define CPSM_RSPCRC_NOCHK (1u << 10)
#define CPSM_WITHDATA (1u << 11)

#define DPSM_ENABLE      (1u << 0)
#define DPSM_DIR_READ    (1u << 1)
#define DPSM_BLKSZ_512   (512u << 16)
#define DPSM_BUS_1BIT    (0u << 3)

#define STA_RESP_END    (1u << 4)
#define STA_RESP_TIMEO  (1u << 2)
#define STA_RESP_CRC    (1u << 0)
#define STA_CMD_SENT    (1u << 5)
#define STA_DATA_END    (1u << 6)
#define STA_DATA_BLKEND (1u << 7)

#define RESP_MASK (STA_RESP_END | STA_RESP_TIMEO | STA_RESP_CRC)

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void small_delay(void)
{
    for (volatile uint32_t i = 0; i < 2000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

#define WAIT_LIMIT 500000u
static uint32_t wait_sta_capture(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t sta = MCI_STA;
        if (sta & mask)
            return ((i + 1u) << 16) | (sta & 0xFFFFu);
    }
    return 0;
}

static int send_resp(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                      uint32_t *resp_out, uint32_t *sta_out)
{
    if (MCI_CMD & CPSM_ENABLE) {
        MCI_CMD = 0;
        small_delay();
    }

    MCI_ARG = arg;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | flags | (cmd_idx << 1);

    uint32_t r = wait_sta_capture(RESP_MASK);
    uint32_t sta = r & 0xFFFFu;
    *sta_out = sta;

    if (!(sta & (STA_RESP_END | STA_RESP_CRC)))
        return -1;

    *resp_out = MCI_RESP0;
    return 0;
}

void stub_main(void)
{
    clear_result();
    OUT[0] = 0x464C4F57u; /* "FLOW" */
    OUT[1] = 1u;

    /* === WORKING INIT SEQUENCE === */

    /* Clock gate reset: SYSCTRL+0x0C bit2 toggle */
    SYSCTRL(0x0C) |=  (1u << 2);
    small_delay();
    SYSCTRL(0x0C) &= ~(1u << 2);
    small_delay();

    /* Sharepin: SET MDAT2 (bit29) + DATA[7:0] + GRP3=MMC */
    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    small_delay();

    /* PUPD pull-up */
    SYSCTRL(0xA0) |= 0x180u;
    small_delay();

    /* CLOCK: ENABLE|FAIL first, then add CLK_EN+PWRSAVE+div */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | MCI_PWRSAVE | 0xF0u;
    small_delay();

    OUT[2] = MCI_CLOCK;
    OUT[3] = MCI_STA;

    /* 74+ clock cycles */
    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    /* Clear CMD, DATACTRL, DMACTRL */
    MCI_CMD = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL = 0;

    MCI_DATATIMER = 0x00030000u;
    MCI_MASK = 0;

    /* 74+ clock cycles */
    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    OUT[2] = MCI_CLOCK;
    OUT[3] = MCI_STA;

    /* CMD0: GO_IDLE_STATE */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    small_delay();

    /* CMD8: SEND_IF_COND (retry up to 10) */
    uint32_t resp8, sta8;
    int rc8 = -1;
    for (uint32_t retry = 0; retry < 10; retry++) {
        if (retry > 0) small_delay();
        rc8 = send_resp(8, 0x1AAu, CPSM_RESPONSE, &resp8, &sta8);
        if (rc8 == 0 && resp8 == 0x1AAu) break;
    }
    OUT[4] = rc8;
    OUT[5] = resp8;
    OUT[6] = sta8;

    if (rc8 < 0 || resp8 != 0x1AAu) {
        OUT[7] = 0xBAD8u;
        return;
    }

    /* ACMD41 loop with OCR feedback */
    uint32_t ocr_arg = 0x40FF8000u;  /* HCS=1 + 2.7-3.6V */
    uint32_t ocr = 0, sta;
    uint32_t attempt;
    for (attempt = 0; attempt < 100; attempt++) {
        uint32_t dummy;
        if (send_resp(55, 0, CPSM_RESPONSE, &dummy, &sta) < 0) break;
        if (send_resp(41, ocr_arg, CPSM_RESPONSE | CPSM_RSPCRC_NOCHK, &ocr, &sta) < 0) break;
        if (ocr & 0x80000000u) break;
        /* OCR feedback: use card's voltage range */
        if (attempt == 0 && (ocr & 0x00FFFFFFu))
            ocr_arg = (ocr & 0x40FF8000u) | 0x40FF8000u;
        small_delay();
    }
    OUT[7] = attempt;
    OUT[8] = ocr;
    int sdhc = (ocr & 0x40000000u) ? 1 : 0;
    OUT[9] = sdhc;

    if (!(ocr & 0x80000000u)) {
        OUT[10] = 0xBAD41u;
        return;
    }

    /* CMD2: ALL_SEND_CID (R2, 136-bit long response) */
    uint32_t sta2;
    MCI_ARG = 0;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | CPSM_LONGRSP | (2u << 1);
    uint32_t r2 = wait_sta_capture(RESP_MASK);
    sta2 = r2 & 0xFFFFu;
    OUT[11] = r2;
    if (!(sta2 & STA_RESP_END)) {
        OUT[12] = 0xBAD2u;
        return;
    }
    /* R2 response is in RESP0..RESP3 (128 bits total, CID) */
    OUT[12] = MCI_RESP0;
    OUT[13] = MCI_RESP1;
    OUT[14] = MCI_RESP2;
    OUT[15] = MCI_RESP3;
    small_delay();

    /* CMD3: SEND_RELATIVE_ADDR (R6) */
    uint32_t rca, sta3;
    if (send_resp(3, 0, CPSM_RESPONSE, &rca, &sta3) < 0) {
        OUT[16] = 0xBAD3u;
        return;
    }
    OUT[16] = rca;
    small_delay();

    /* CMD7: SELECT_CARD with RCA (bits [31:16] from R6) */
    uint32_t sel_arg = rca & 0xFFFF0000u;
    uint32_t sel_resp, sta7;
    int rc7 = send_resp(7, sel_arg, CPSM_RESPONSE, &sel_resp, &sta7);
    OUT[17] = sel_resp;
    OUT[39] = sel_arg;   /* debug: CMD7 arg */
    OUT[40] = rca;       /* debug: raw RCA from CMD3 */
    if (rc7 < 0) {
        OUT[18] = 0xBAD7u;
        return;
    }
    small_delay();

    /* CMD17: EXACT match of original sd_cmd17_v2 working code */

    /* CMD17: READ_SINGLE_BLOCK with PIO mode (DMACTRL=0) */
    MCI_DMACTRL = (1u << 0) | (128u << 17); /* DMA_BUFEN + DMA_SIZE=128, DMA_EN=0 */
    MCI_CMD = 0;
    MCI_DATACTRL = 0;
    MCI_DATALEN = 0;
    MCI_DMACTRL = 0;  /* PIO mode: internal FIFO */

    /* Set up data transfer: 1 block of 512 bytes, read direction */
    MCI_DATALEN = 512u;
    MCI_DATACTRL = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512 | DPSM_BUS_1BIT;

    /* CMD17: READ_SINGLE_BLOCK (CPSM_RESPONSE=0x80, no WITHDATA per WinCE driver) */
    MCI_ARG = 0;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (17u << 1);

    /* Wait for command response */
    uint32_t r17 = wait_sta_capture(RESP_MASK);
    uint32_t sta17 = r17 & 0xFFFFu;
    OUT[18] = r17;
    if (!(sta17 & STA_RESP_END)) {
        OUT[19] = 0xBAD17Cu;
        return;
    }
    OUT[19] = MCI_RESP0;

    /* Read from internal FIFO (DMACTRL=0 -> PIO mode) */
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)(MCI_BASE + 0x40u);
    for (int i = 0; i < 128; i++) {
        /* Wait for FIFO to have data */
        uint32_t j;
        for (j = 0; j < 50000u; j++) {
            if (MCI_STA & ((1u << 14) | (1u << 12)))
                break;
        }
        uint32_t w = *fifo;
        if (i < 16) OUT[20 + i] = w;
        if (i == 127) OUT[35] = w;
    }
    OUT[37] = MCI_STA;

    /* Extract MBR signature from last word (bytes 510-511) */
    OUT[38] = (OUT[35] >> 16) & 0xFFu; /* byte 510 */
    OUT[39] = (OUT[35] >> 24) & 0xFFu; /* byte 511 */
}
