/*
 * sd_init_v2_probe - fast version without long delays
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
#define MCI_DATATIMER MCI(0x24)

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

#define RESP_MASK (STA_RESP_END | STA_RESP_TIMEO | (1u << 0))

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

/*
 * Wait for any bit in mask. Returns (iter+1)<<16 | STA_at_detection.
 * Returns 0 on timeout.
 */
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

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x494E5632u; /* "INV2" */
    OUT[1] = 1u;

    /* Reset MMC */
    uint32_t clk_con = SYSCTRL(0x0C);
    SYSCTRL(0x0C) = clk_con | CLK_MMC_BIT;
    small_delay();
    SYSCTRL(0x0C) = clk_con & ~CLK_MMC_BIT;
    small_delay();

    /* Sharepin DATA pins */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    micro_delay();

    /* PUPD2 pull-up (bootrom style) */
    SYSCTRL(0xA0) |= 0x180u;
    micro_delay();

    /* Try also enabling pull-up on PUPD1 and PUPD3 in case GPIO39 is elsewhere */
    SYSCTRL(0x9C) |= 0x180u;   /* PUPD1 bits 7,8 */
    SYSCTRL(0xA4) |= 0x180u;   /* PUPD3 bits 7,8 */

    OUT[2] = SYSCTRL(0x9C);   /* PUPD1 */
    OUT[3] = SYSCTRL(0xA0);   /* PUPD2 */
    OUT[4] = SYSCTRL(0xA4);   /* PUPD3 */

    /* MCI init with bootrom clock div */
    MCI_CLOCK = 0;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();

    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    small_delay();

    /* Set datatimer for reasonable response timeout (~0.5s at 400kHz) */
    MCI_DATATIMER = 0x00030000u;  /* ~200k clock cycles */
    MCI_MASK = 0xFFFFFFFFu;

    OUT[5] = MCI_CLOCK;
    OUT[6] = MCI_DATATIMER;

    /* Provide ~74+ clock cycles before CMD0 */
    for (volatile uint32_t i = 0; i < 10000u; i++)
        __asm__ volatile ("" : : : "memory");

    OUT[7] = MCI_STA;

    /* CMD0: GO_IDLE_STATE */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;

    uint32_t r0 = wait_sta_capture(STA_CMD_SENT | STA_RESP_TIMEO);
    OUT[8] = r0;
    OUT[9] = MCI_STA;
    OUT[10] = MCI_RESP0;

    /* Small delay between CMD0 and CMD8 */
    small_delay();

    /* CMD8: SEND_IF_COND */
    MCI_ARG = 0x1AAu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);

    uint32_t r8 = wait_sta_capture(RESP_MASK);
    OUT[11] = r8;
    OUT[12] = MCI_STA;
    OUT[13] = MCI_RESP0;
    OUT[14] = MCI_RESPCMD;
    OUT[15] = MCI(0x0C);  /* CMD readback */

    /* Try a brute-force CMD8 with all writable bits set */
    if (!((r8 & 0xFFFF) & STA_RESP_END)) {
        small_delay();
        MCI_ARG = 0x1AAu;
        MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);

        /* capture STA very fast after write */
        OUT[16] = MCI_STA;
        OUT[17] = MCI_STA;
        OUT[18] = MCI_STA;
        OUT[19] = MCI_STA;

        /* Wait longer */
        uint32_t r8b = wait_sta_capture(RESP_MASK);
        OUT[20] = r8b;
        OUT[21] = MCI_RESP0;
    }

    /* Try at half clock speed (div=480) */
    MCI_CLOCK = 0;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | (0x1E0u & 0xFF) | ((0x1E0u & 0xFF) << 8);
    small_delay();

    OUT[22] = MCI_CLOCK;

    MCI_ARG = 0x1AAu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);
    uint32_t r8_slow = wait_sta_capture(RESP_MASK);
    OUT[23] = r8_slow;
    OUT[24] = MCI_STA;
    OUT[25] = MCI_RESP0;

    /* Try with PWRSAVE off */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    small_delay();

    MCI_ARG = 0x1AAu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);
    uint32_t r8_nops = wait_sta_capture(RESP_MASK);
    OUT[26] = r8_nops;
    OUT[27] = MCI_RESP0;
}
