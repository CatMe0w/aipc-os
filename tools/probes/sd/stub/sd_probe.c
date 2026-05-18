#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off) REG32(SYSCTRL_BASE + (off))

#define MCI_BASE 0x20020000u
#define MCI(off) REG32(MCI_BASE + (off))

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_RESP0     MCI(0x14)
#define MCI_RESP1     MCI(0x18)
#define MCI_RESP2     MCI(0x1C)
#define MCI_RESP3     MCI(0x20)
#define MCI_STA       MCI(0x34)

#define STA_CMD_SENT  (1u << 5)
#define STA_RESP_END  (1u << 4)
#define STA_RESP_TO   (1u << 2)

#define WAIT_LIMIT    2000000u

enum {
    STAGE_CLOCK_RESET = 0,
    STAGE_SHAREPIN    = 1,
    STAGE_CLOCK_ON    = 2,
    STAGE_CMD0        = 3,
    STAGE_CMD8        = 4,
    STAGE_ACMD41      = 5,
    STAGE_CMD2        = 6,
};

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void delay_long(void)
{
    for (volatile uint32_t i = 0; i < 100000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 128u; i++)
        OUT[i] = 0;
}

static uint32_t wait_reg_mask(uint32_t addr, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        if ((REG32(addr) & mask) == value)
            return i + 1u;
    }
    return 0;
}

static uint32_t wait_reg_any(uint32_t addr, uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        if (REG32(addr) & mask)
            return i + 1u;
    }
    return 0;
}

static void record_state(uint32_t slot, uint32_t stage)
{
    volatile uint32_t *p = OUT + 4u + slot * 12u;

    p[0] = stage;
    p[1] = SYSCTRL(0x0C);
    p[2] = SYSCTRL(0x10);
    p[3] = SYSCTRL(0x74);
    p[4] = SYSCTRL(0x78);
    p[5] = SYSCTRL(0xBC);
    p[6] = MCI_CLOCK;
    p[7] = MCI_STA;
    p[8] = MCI_RESP0;
    p[9] = MCI_RESP1;
    p[10] = MCI_RESP2;
    p[11] = MCI_RESP3;
}

static void record_card_detect(uint32_t slot, uint32_t cd)
{
    volatile uint32_t *p = OUT + 4u + slot * 12u;

    p[0] = 0xCDu;
    p[1] = cd;
    p[2] = SYSCTRL(0xBC);
    p[3] = 0;
    p[4] = 0;
    p[5] = 0;
    p[6] = 0;
    p[7] = 0;
    p[8] = 0;
    p[9] = 0;
    p[10] = 0;
    p[11] = 0;
}

void stub_main(void)
{
    uint32_t wait_ticks;
    uint32_t ocr;

    clear_result();

    OUT[0] = 0x53445052u; /* "SDPR" */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;
    OUT[3] = 0;

    /* clock gate + reset */
    SYSCTRL(0x0C) &= ~(1u << 2);
    SYSCTRL(0x10) |= (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();
    record_state(0, STAGE_CLOCK_RESET);

    /* sharepin: 4-bit SD */
    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 5)) | (2u << 5);
    delay();
    record_state(1, STAGE_SHAREPIN);

    /* card detect */
    record_card_detect(2, (SYSCTRL(0xBC) >> 13) & 1u);

    /* start MMC clock ~200 kHz */
    MCI_CLOCK = (1u << 20) | (1u << 19) | 240;
    delay_long();
    MCI_CLOCK = (1u << 20) | (1u << 19) | (1u << 16) | 240;
    delay_long();
    (void)MCI_STA;
    record_state(3, STAGE_CLOCK_ON);

    /* CMD0: GO_IDLE */
    MCI_ARG = 0;
    MCI_CMD = 0;
    wait_ticks = wait_reg_mask(MCI_BASE + 0x34, STA_CMD_SENT, STA_CMD_SENT);
    OUT[4 + 4 * 12 + 0] = wait_ticks;
    OUT[4 + 4 * 12 + 1] = MCI_STA;
    delay_long();
    record_state(4, STAGE_CMD0);

    /* CMD8: SEND_IF_COND */
    MCI_ARG = 0x000001AAu;
    MCI_CMD = (1u << 6) | (1u << 8) | 8;
    wait_ticks = wait_reg_any(MCI_BASE + 0x34, STA_RESP_END | STA_RESP_TO);
    OUT[4 + 4 * 12 + 2] = wait_ticks;
    OUT[4 + 4 * 12 + 3] = MCI_STA;
    OUT[4 + 4 * 12 + 4] = MCI_RESP0;
    delay();
    record_state(5, STAGE_CMD8);

    /* ACMD41 loop */
    ocr = 0;
    for (uint32_t i = 0; i < 80; i++) {
        /* CMD55 */
        MCI_ARG = 0;
        MCI_CMD = (1u << 6) | (1u << 8) | 55;
        wait_reg_any(MCI_BASE + 0x34, STA_RESP_END | STA_RESP_TO);
        delay();

        /* ACMD41 */
        MCI_ARG = 0x40FF8000u;
        MCI_CMD = (1u << 6) | 41;
        wait_reg_any(MCI_BASE + 0x34, STA_RESP_END | STA_RESP_TO);
        ocr = MCI_RESP0;
        delay();
        if (ocr & 0x80000000u)
            break;
    }
    OUT[4 + 4 * 12 + 5] = ocr;
    record_state(6, STAGE_ACMD41);

    /* CMD2: ALL_SEND_CID (if card is ready) */
    if (ocr & 0x80000000u) {
        MCI_ARG = 0;
        MCI_CMD = (1u << 6) | (1u << 7) | (1u << 8) | 2;
        wait_ticks = wait_reg_any(MCI_BASE + 0x34, STA_RESP_END | STA_RESP_TO);
        OUT[4 + 4 * 12 + 6] = wait_ticks;
        OUT[4 + 4 * 12 + 7] = MCI_STA;
        OUT[4 + 4 * 12 + 8] = MCI_RESP0;
        OUT[4 + 4 * 12 + 9] = MCI_RESP1;
        OUT[4 + 4 * 12 + 10] = MCI_RESP2;
        OUT[4 + 4 * 12 + 11] = MCI_RESP3;
        record_state(7, STAGE_CMD2);
    }
}
