/*
 * sd_full_init_v2 - fixed ACMD41 (accept RESP_CRC for R3 responses)
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
#define MCI_RESPCMD   MCI(0x10)
#define MCI_DATATIMER MCI(0x24)

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

#define RESP_MASK (STA_RESP_END | STA_RESP_TIMEO | STA_RESP_CRC)

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

/*
 * Send command with response. Returns 0 on RESP_END (with or without CRC).
 * For R3/R4 responses (no CRC), RESP_CRC is normal and accepted.
 */
static int send_cmd_resp(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                          uint32_t *resp_out, uint32_t *sta_out)
{
    MCI_ARG = arg;
    MCI_CMD = CPSM_ENABLE | flags | (cmd_idx << 1);

    uint32_t r = wait_sta_capture(RESP_MASK);
    uint32_t sta = r & 0xFFFFu;
    *sta_out = sta;

    if (!(sta & (STA_RESP_END | STA_RESP_CRC)))
        return -1; /* no response / timeout */

    *resp_out = MCI_RESP0;
    return 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x46494E32u; /* "FIN2" */
    OUT[1] = 2u;

    /* Reset MMC */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT;
    small_delay();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT;
    small_delay();

    /* Sharepin DATA[7:0] */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    micro_delay();

    /* PUPD pull-up */
    SYSCTRL(0x9C) |= 0x180u;
    SYSCTRL(0xA0) |= 0x180u;
    SYSCTRL(0xA4) |= 0x180u;
    micro_delay();

    /* MCI init */
    MCI_CLOCK = 0; small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL; small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    small_delay();

    MCI_DATATIMER = 0x00030000u;
    MCI_MASK = 0xFFFFFFFFu;

    /* 74+ clocks */
    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    OUT[2] = MCI_CLOCK;
    OUT[3] = MCI(0x34);

    /* CMD0: GO_IDLE_STATE */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    uint32_t r0 = wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    OUT[4] = r0;

    small_delay();

    /* CMD8: SEND_IF_COND */
    uint32_t resp8, sta8;
    int rc8 = send_cmd_resp(8, 0x1AAu, CPSM_RESPONSE, &resp8, &sta8);
    OUT[5] = rc8;
    OUT[6] = resp8;
    OUT[7] = sta8;

    if (rc8 < 0 || resp8 != 0x1AAu) {
        OUT[8] = 0xBAD00001u;
        return;
    }

    /* CMD55: APP_CMD prefix (RCA=0 during init) */
    uint32_t resp55, sta55;
    int rc55 = send_cmd_resp(55, 0, CPSM_RESPONSE, &resp55, &sta55);
    OUT[8] = rc55;
    OUT[9] = resp55;
    OUT[10] = sta55;

    if (rc55 < 0) {
        OUT[11] = 0xBAD55000u;
        return;
    }

    /* ACMD41: SD_SEND_OP_COND with HCS=1 (support SDHC) */
    uint32_t ocr, sta41;
    int rc41 = send_cmd_resp(41, 0x40000000u, CPSM_RESPONSE, &ocr, &sta41);
    OUT[11] = rc41;
    OUT[12] = ocr;
    OUT[13] = sta41;

    if (rc41 < 0) {
        OUT[14] = 0xBAD41001u;
        return;
    }

    /* Check if card is ready (OCR bit31) */
    if (ocr & 0x80000000u) {
        OUT[14] = ocr;
        OUT[15] = (ocr & 0x40000000u) ? 0x53444843u : 0x53445343u;
        OUT[16] = (ocr >> 16) & 0xFFu;
    } else {
        /* Card not ready yet - try a few more times */
        uint32_t attempt;
        for (attempt = 0; attempt < 100; attempt++) {
            small_delay();

            send_cmd_resp(55, 0, CPSM_RESPONSE, &resp55, &sta55);
            rc41 = send_cmd_resp(41, 0x40000000u, CPSM_RESPONSE, &ocr, &sta41);
            if (rc41 < 0) break;
            if (ocr & 0x80000000u) break;
        }
        OUT[14] = ocr;
        OUT[15] = (ocr & 0x40000000u) ? 0x53444843u : 0x53445343u;
        OUT[16] = attempt;
    }
}
