/*
 * sd_acmd41_loop - ACMD41 initialization loop with OCR feedback
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

    OUT[0] = 0x41434D44u; /* "ACMD" */
    OUT[1] = 3u;

    /* Reset */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT; small_delay();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT; small_delay();

    /* Sharepin: SET MDAT2 (bit29) + GRP3/GRP4 = MMC */
    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~((3u << 3) | (3u << 5))) | (2u << 3) | (2u << 5);
    micro_delay();

    /* PUPD */
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

    /* 74 clocks */
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
        OUT[2] = 0xBAD00001u;
        return;
    }
    OUT[2] = resp8;
    OUT[3] = sta8;

    /* CMD55 + ACMD41 loop with OCR feedback */
    uint32_t ocr_arg = 0x40000000u;  /* start with HCS=1 */
    uint32_t ocr, sta;
    uint32_t attempt;
    int found_ocr = 0;

    for (attempt = 0; attempt < 100; attempt++) {
        /* CMD55 */
        uint32_t dummy, st;
        if (send_resp(55, 0, &dummy, &st) < 0) {
            OUT[4] = 0xBAD55000u | attempt;
            return;
        }

        /* ACMD41 */
        if (send_resp(41, ocr_arg, &ocr, &sta) < 0) {
            OUT[5] = 0xBAD41000u | attempt;
            return;
        }

        /* If first response, capture card OCR for feedback */
        if (!found_ocr && (ocr & 0x00FFFFFFu)) {
            found_ocr = 1;
            /* Use card's voltage range as argument for subsequent calls */
            ocr_arg = ocr & 0x40FF8000u;  /* keep HCS if present, use card's VDD */
            OUT[6] = ocr_arg;  /* feedback arg used */
        }

        if (ocr & 0x80000000u) {
            /* Card ready! */
            OUT[4] = attempt;
            OUT[5] = ocr;
            OUT[7] = (ocr & 0x40000000u) ? 0x53444843u : 0x53445343u;
            OUT[8] = ocr & 0x00FFFFFFu;
            OUT[9] = sta;
            return;
        }

        /* Check if HCS is being rejected: try without HCS */
        if (attempt == 50 && !(ocr & 0x80000000u)) {
            ocr_arg = 0x00FF8000u;  /* drop HCS, try with card's VDD only */
            OUT[10] = ocr_arg;
        }

        small_delay();
    }

    OUT[4] = attempt;
    OUT[5] = ocr;
    OUT[11] = 0x544F5554u;  /* "TOUT" timeout */
}
