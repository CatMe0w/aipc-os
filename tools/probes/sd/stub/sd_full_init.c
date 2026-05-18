/*
 * sd_full_init - complete SD card initialization sequence
 * CMD0 -> CMD8 -> ACMD41 loop -> OCR
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

static void micro_delay(void)
{
    for (volatile uint32_t i = 0; i < 200u; i++)
        __asm__ volatile ("" : : : "memory");
}

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

/* Send a command and wait for response. Returns 0 on success, <0 on error. */
static int send_cmd(uint32_t cmd_idx, uint32_t arg, uint32_t flags, uint32_t *resp_out)
{
    MCI_ARG = arg;
    uint32_t cmd = CPSM_ENABLE | flags | (cmd_idx << 1);
    MCI_CMD = cmd;

    uint32_t r = wait_sta_capture(RESP_MASK);
    uint32_t sta = r & 0xFFFFu;

    if (sta & STA_RESP_TIMEO) return -1;
    if (sta & STA_RESP_CRC)   return -2;
    if (sta & STA_RESP_END) {
        *resp_out = MCI_RESP0;
        return 0;
    }
    return -3; /* no response */
}

/* Send CMD55 (APP_CMD prefix) */
static int send_cmd55(uint32_t rca, uint32_t *resp_out)
{
    MCI_ARG = rca << 16;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (55u << 1);

    uint32_t r = wait_sta_capture(RESP_MASK);
    uint32_t sta = r & 0xFFFFu;

    if (sta & STA_RESP_END) {
        *resp_out = MCI_RESP0;
        return 0;
    }
    return -1;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x46494E49u; /* "FINI" */
    OUT[1] = 1u;

    /* --- Reset MMC --- */
    uint32_t clk_con = SYSCTRL(0x0C);
    SYSCTRL(0x0C) = clk_con | CLK_MMC_BIT;
    small_delay();
    SYSCTRL(0x0C) = clk_con & ~CLK_MMC_BIT;
    small_delay();

    /* Sharepin */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    micro_delay();

    /* PUPD pull-up on all relevant registers */
    SYSCTRL(0x9C) |= 0x180u;
    SYSCTRL(0xA0) |= 0x180u;
    SYSCTRL(0xA4) |= 0x180u;
    micro_delay();

    /* MCI init */
    MCI_CLOCK = 0;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    small_delay();

    /* Critical: DATATIMER must be set for proper response timeout */
    MCI_DATATIMER = 0x00030000u;
    MCI_MASK = 0xFFFFFFFFu;

    /* 74+ clock cycles before CMD0 */
    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    OUT[2] = MCI_CLOCK;
    OUT[3] = MCI_STA;

    /* === CMD0: GO_IDLE_STATE === */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    uint32_t r0 = wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    OUT[4] = r0;

    small_delay();

    /* === CMD8: SEND_IF_COND === */
    uint32_t resp8;
    int rc8 = send_cmd(8, 0x1AAu, CPSM_RESPONSE, &resp8);
    OUT[5] = rc8;
    OUT[6] = resp8;

    if (rc8 != 0) {
        OUT[7] = 0xBAD00001u;  /* CMD8 failed */
        return;
    }
    if (resp8 != 0x1AAu) {
        OUT[7] = 0xBAD00002u;  /* CMD8 response mismatch */
        return;
    }
    OUT[7] = 0x4F4B3830u;  /* "OK80" */

    /* === ACMD41 loop === */
    uint32_t ocr;
    uint32_t acmd41_ok = 0;
    for (uint32_t attempt = 0; attempt < 200; attempt++) {
        uint32_t resp55;
        if (send_cmd55(0, &resp55) < 0) {
            OUT[8] = attempt;
            OUT[9] = 0xBAD55000u;  /* CMD55 failed */
            goto done;
        }

        /* ACMD41: HCS=1 (support SDHC), voltage=0x40000000 */
        int rc41 = send_cmd(41, 0x40000000u, CPSM_RESPONSE, &ocr);
        if (rc41 < 0) {
            OUT[8] = attempt;
            OUT[9] = 0xBAD41000u;
            goto done;
        }

        /* Check if card is ready (bit31 = busy) */
        if (ocr & 0x80000000u) {
            acmd41_ok = 1;
            OUT[8] = attempt;       /* number of attempts */
            OUT[9] = ocr;           /* final OCR */
            break;
        }

        small_delay();
    }

    if (!acmd41_ok) {
        OUT[9] = 0xBAD00003u;  /* ACMD41 timed out */
        goto done;
    }

    OUT[10] = ocr;  /* OCR value */
    OUT[11] = (ocr & 0x40000000u) ? 0x53444843u : 0x53445343u;  /* "SDHC" or "SDSC" */
    OUT[12] = (ocr >> 16) & 0xFFu;  /* voltage window (bits 23:16) */

done:
    return;
}
