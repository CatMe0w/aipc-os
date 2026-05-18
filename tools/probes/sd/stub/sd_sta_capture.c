/*
 * sd_sta_capture - capture MCI_STATUS at the exact moment it changes,
 * and try pull-up re-enable after reset.
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

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)
#define MCI_PWRSAVE   (1u << 17)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)
#define CPSM_LONGRSP  (1u << 8)

#define CLK_MMC_BIT   (1u << 2)

#define STA_CMD_SENT    (1u << 5)
#define STA_RESP_END    (1u << 4)
#define STA_RESP_TIMEO  (1u << 2)
#define STA_RESP_CRC    (1u << 0)
#define STA_CMD_ACTIVE  (1u << 9)

#define WAIT_LIMIT    2000000u
#define N_SAMPLES     16u

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

/*
 * Wait for any bit in mask, capturing STA at detection point.
 * Returns: upper 16 bits = iteration count, lower 16 bits = STA value at detection.
 * Returns 0 if timeout.
 */
static uint32_t wait_sta_capture(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t sta = MCI_STA;
        if (sta & mask)
            return ((i + 1u) << 16) | (sta & 0xFFFFu);
    }
    return 0;
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x53544143u; /* "STAC" */
    OUT[1] = 1u;

    /* Reset MMC via clock gate toggle */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT;
    delay_long();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT;
    delay_long();

    /* Sharepin for DATA[7:0] via ePIN_AS_SDMMC1 */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    /* Explicit pull-up enable: PUPD2 bits 7,8 = 1 (bootrom style) */
    SYSCTRL(0xA0) |= 0x180u;   /* mimic bootrom: pull-up on GPIO39? and GPIO40? */
    delay();

    OUT[2] = SYSCTRL(0xA0);  /* PUPD2 after re-enable */

    /* MCI init: ENABLE | FAIL | CLK_EN | div=0xF0 (bootrom-style divider) */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();

    /* Use bootrom divider (0xF0) + CLK_EN, no PWRSAVE this time */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    delay_long();

    MCI_MASK = 0xFFFFFFFFu;

    OUT[3]  = MCI_CLOCK;
    OUT[4]  = MCI_STA;          /* STA before any command */

    /* Send raw CMD0 with CPSM_ENABLE */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;      /* opcode 0 + CPSM_ENABLE */

    /* Sample STA multiple times very fast */
    for (uint32_t n = 0; n < N_SAMPLES; n++) {
        OUT[10 + n] = MCI_STA;
    }

    uint32_t r0 = wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    OUT[5] = r0;                /* [iter<<16 | STA] for CMD0 */
    OUT[6] = MCI_STA;           /* immediate post-wait STA */
    OUT[7] = MCI_CMD;           /* CMD readback */

    /* CMD8: opcode 8, CPSM_ENABLE + CPSM_RESPONSE (no LONGRSP!) */
    MCI_ARG = 0x1AAu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);

    /* Sample STA multiple times very fast */
    for (uint32_t n = 0; n < N_SAMPLES; n++) {
        OUT[26 + n] = MCI_STA;
    }

    uint32_t r8 = wait_sta_capture(STA_CMD_SENT | STA_RESP_END | STA_RESP_TIMEO | STA_RESP_CRC);
    OUT[8] = r8;                /* [iter<<16 | STA] for CMD8 */
    OUT[9] = MCI_STA;           /* immediate post-wait STA */
    OUT[42] = MCI_CMD;           /* CMD readback */
    OUT[43] = MCI_RESP0;        /* response (if any) */
    OUT[44] = MCI_RESPCMD;      /* response command index (if any) */
}
