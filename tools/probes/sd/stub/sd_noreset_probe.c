/*
 * sd_noreset_probe - skip clock gate reset, use bootrom MCI state directly
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
#define MCI_PWRSAVE   (1u << 17)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)
#define CPSM_RSPCRC_NOCHK (1u << 10)

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

static int send_cmd_resp(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                          uint32_t *resp_out, uint32_t *sta_out)
{
    /* Driver-style: abort any stuck CPSM before sending */
    if (MCI_CMD & CPSM_ENABLE) {
        MCI_CMD = 0;
        small_delay();
    }

    MCI_ARG = arg;
    /* Driver-style: enable only CMD IRQ mask bits */
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

    OUT[0] = 0x4E4F5245u; /* "NORE" = NO REset */
    OUT[1] = 1u;

    /* NO clock gate reset - use bootrom's existing MCI state */

    /* Sharepin DATA[7:0] - leave bit29 as-is (bootrom default) */
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    small_delay();

    /* PUPD pull-up */
    SYSCTRL(0xA0) |= 0x180u;
    small_delay();

    OUT[2] = MCI_CLOCK;
    OUT[3] = MCI_STA;

    /* EXACT AK98 driver probe init sequence */
    /* Reset: SYSCTRL+0x10 bit18 (AK98 style, but try it) */
    SYSCTRL(0x10) |=  (1u << 18);
    small_delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    small_delay();

    /* Sharepin (group_pin_config) */
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    small_delay();

    /* PUPD pull-up */
    SYSCTRL(0xA0) |= 0x180u;
    small_delay();

    /* MCI_CLOCK: ENABLE|FAIL only (no CLK_EN yet) - matches probe */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();

    /* Later add CLK_EN + PWRSAVE (matches set_ios) */
    uint32_t clk = MCI_CLOCK;
    clk |= MCI_CLK_EN | MCI_PWRSAVE;
    clk &= ~0xFFFFu;
    clk |= 0xF0u;
    MCI_CLOCK = clk;
    small_delay();

    /* Clear CMD, DATACTRL, DMACTRL */
    MCI_CMD = 0;
    MCI(0x2C) = 0;
    MCI(0x3C) = 0;

    /* DATATIMER, MASK=0 (exact match) */
    MCI_DATATIMER = 0x00030000u;
    MCI_MASK = 0;

    /* Provide 74+ clock cycles (bootrom clock is already running) */
    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    /* CMD0: GO_IDLE_STATE */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    uint32_t r0 = wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    OUT[4] = r0;

    small_delay();

    /* CMD8: SEND_IF_COND - retry up to 10 times */
    uint32_t resp8, sta8;
    int rc8 = -1;
    for (uint32_t retry = 0; retry < 10; retry++) {
        if (retry > 0)
            small_delay();
        rc8 = send_cmd_resp(8, 0x1AAu, CPSM_RESPONSE, &resp8, &sta8);
        if (rc8 == 0 && resp8 == 0x1AAu)
            break;
    }
    OUT[5] = rc8;
    OUT[6] = resp8;
    OUT[7] = sta8;

    if (rc8 < 0 || resp8 != 0x1AAu) {
        OUT[8] = 0xBAD00001u;
        return;
    }

    /* CMD55 */
    uint32_t resp55, sta55;
    int rc55 = send_cmd_resp(55, 0, CPSM_RESPONSE, &resp55, &sta55);
    OUT[8] = rc55;
    OUT[9] = resp55;
    OUT[10] = sta55;

    if (rc55 < 0) {
        OUT[11] = 0xBAD55000u;
        return;
    }

    /* ACMD41 */
    uint32_t ocr, sta41;
    int rc41 = send_cmd_resp(41, 0x40FF8000u, CPSM_RESPONSE | CPSM_RSPCRC_NOCHK, &ocr, &sta41);
    OUT[11] = rc41;
    OUT[12] = ocr;
    OUT[13] = sta41;

    if (rc41 < 0) {
        OUT[14] = 0xBAD41001u;
        return;
    }

    if (ocr & 0x80000000u) {
        OUT[14] = ocr;
        OUT[15] = (ocr & 0x40000000u) ? 0x53444843u : 0x53445343u;
    } else {
        uint32_t attempt;
        for (attempt = 0; attempt < 100; attempt++) {
            small_delay();
            send_cmd_resp(55, 0, CPSM_RESPONSE, &resp55, &sta55);
            rc41 = send_cmd_resp(41, ocr & 0x40FF8000u, CPSM_RESPONSE | CPSM_RSPCRC_NOCHK, &ocr, &sta41);
            if (rc41 < 0) break;
            if (ocr & 0x80000000u) break;
        }
        OUT[14] = ocr;
        OUT[15] = (ocr & 0x40000000u) ? 0x53444843u : 0x53445343u;
        OUT[16] = attempt;
    }
}
