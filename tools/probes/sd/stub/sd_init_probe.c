/*
 * sd_init_probe - proper SD init with 74+ clock cycles, CMD0, delay, CMD8.
 *
 * SD spec requires:
 * 1. Power on the card
 * 2. Send at least 74 clock cycles with CMD/DAT high (pull-up)
 * 3. CMD0 (GO_IDLE_STATE)
 * 4. Wait (card needs time)
 * 5. CMD8 (SEND_IF_COND) to check voltage
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
#define MCI_RESPCMD   MCI(0x10)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)
#define MCI_PWRSAVE   (1u << 17)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)

#define CLK_MMC_BIT   (1u << 2)

#define STA_CMD_SENT    (1u << 5)
#define STA_RESP_END    (1u << 4)
#define STA_RESP_TIMEO  (1u << 2)
#define STA_CMD_ACTIVE  (1u << 9)

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void delay_long(void)
{
    for (volatile uint32_t i = 0; i < 200000u; i++)
        __asm__ volatile ("" : : : "memory");
}

/* Longer delay: ~50ms worth of loops */
static void delay_50ms(void)
{
    for (volatile uint32_t i = 0; i < 2000000u; i++)
        __asm__ volatile ("" : : : "memory");
}

/*
 * Wait for any bit in mask. Returns (iter+1)<<16 | STA_at_detection.
 * Returns 0 on timeout.
 */
#define WAIT_LIMIT 2000000u
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
 * Wait specifically for RESP_TIMEO | RESP_END | RESP_CRC_FAIL.
 * Returns faster with iter info embedded (high 16=iter, low 16=STA).
 */
static inline uint32_t wait_response(void)
{
    return wait_sta_capture(STA_RESP_END | STA_RESP_TIMEO | (1u << 0));
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x494E4954u; /* "INIT" */
    OUT[1] = 2u;

    /* Reset MMC via clock gate toggle */
    uint32_t clk_con = SYSCTRL(0x0C);
    SYSCTRL(0x0C) = clk_con | CLK_MMC_BIT;
    delay_long();
    SYSCTRL(0x0C) = clk_con & ~CLK_MMC_BIT;
    delay_long();

    /* Sharepin DATA[7:0] as MMC */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /* Re-enable CMD pull-up (bootrom style: PUPD2 bits 7,8 = 1) */
    SYSCTRL(0xA0) |= 0x180u;
    delay();

    /* SD card power: check if GPIO controls it */
    /* GPIO1 (bank0) direction for SD_CD# is input, check if other bits control power */
    OUT[2] = SYSCTRL(0xA0);  /* PUPD2 */

    /* Enable MMC clock to provide initialization clocks */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();

    /*
     * Set clock: use div=240 (0xF0) which the bootrom used.
     * Target is ~400 kHz. With asic_clk unknown, use bootrom default.
     */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    delay_long();  /* let clock stabilize */

    OUT[3] = MCI_CLOCK;

    /*
     * SD spec: send at least 74 clock cycles before CMD0.
     * At ~400 kHz, 74 cycles = 185 μs. We delay much longer to be safe.
     */
    delay_50ms();  /* provides >> 74 clock cycles */

    OUT[4] = MCI_STA;  /* STA before CMD0 */

    /* CMD0 (GO_IDLE_STATE) */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;

    uint32_t r_cmd0 = wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    OUT[5] = r_cmd0;
    OUT[6] = MCI_STA;

    /*
     * After CMD0, card enters idle. Wait some time before next command.
     * Some cards need up to 5ms. We wait much longer.
     */
    for (volatile uint32_t i = 0; i < 500000u; i++)
        __asm__ volatile ("" : : : "memory");

    /* CMD8 (SEND_IF_COND) */
    MCI_ARG = 0x1AAu;   /* VHS=1 (2.7-3.6V), pattern=0xAA */
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);

    uint32_t r_cmd8 = wait_response();
    OUT[7] = r_cmd8;             /* iter<<16 | STA */
    OUT[8] = MCI_STA;
    OUT[9] = MCI_RESP0;
    OUT[10] = MCI_RESPCMD;
    OUT[11] = MCI(0x0C);  /* CMD readback */

    /* If RESP_END for CMD8, try ACMD41 (via CMD55+ACMD41) */
    uint32_t sta_cmd8 = r_cmd8 & 0xFFFFu;
    if (sta_cmd8 & STA_RESP_END) {
        /* Got response to CMD8 - continue with ACMD41 */

        /* CMD55 (APP_CMD): next command is application-specific */
        MCI_ARG = 0;
        MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (55u << 1);
        uint32_t r_cmd55 = wait_response();
        OUT[12] = r_cmd55;

        if ((r_cmd55 & 0xFFFFu) & STA_RESP_END) {
            /* ACMD41: SD_SEND_OP_COND */
            MCI_ARG = 0x40000000u;  /* HCS=1 (support SDHC), OCR=0 */
            MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (41u << 1);
            uint32_t r_acmd41 = wait_response();
            OUT[13] = r_acmd41;
            OUT[14] = MCI_RESP0;
        } else {
            OUT[13] = 0xDEADBEEFu;
        }
    } else {
        OUT[12] = 0;
        OUT[13] = 0;
        OUT[14] = 0;
    }

    /* Also test at SDIO controller base (0x20021000) */
    /* Clock gate for SDIO: SYSCTRL+0x0C bit8 */
    SYSCTRL(0x0C) &= ~(1u << 8);   /* enable SDIO clock */
    delay_long();

    /* SDIO sharepin: already configured for WiFi in bootrom? */
    /* SDIO uses GPIO[29:24] for CMD/CLK/DATA - are these muxed? */
    OUT[15] = SYSCTRL(0x74);  /* sharepin CON2 */
    OUT[16] = SYSCTRL(0x78);  /* sharepin CON1 */
}
