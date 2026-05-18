/*
 * sd_ioctrl_probe - configure I/O control register (SYSCTRL+0xD4) per sdhc_anyka.dll
 *
 * The SDHC driver sets SYSCTRL+0xD4 bit0 for MMC and modifies PUPD registers.
 * None of the previous probes touched these. This probe verifies whether
 * the I/O pad control is the missing piece preventing card communication.
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

static uint32_t wait_sta_any(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        if (MCI_STA & mask)
            return i + 1u;
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

    OUT[0] = 0x494F4354u; /* "IOCT" */

    /* snapshot before any changes */
    OUT[1] = SYSCTRL(0x0C);  /* clock gate */
    OUT[2] = SYSCTRL(0xD4);  /* I/O control before */
    OUT[3] = SYSCTRL(0x9C);  /* PUPD1 */
    OUT[4] = SYSCTRL(0xA0);  /* PUPD2 */
    OUT[5] = MCI_STA;

    /* MMC reset via clock gate toggle */
    SYSCTRL(0x0C) |=  CLK_MMC_BIT;
    delay_long();
    SYSCTRL(0x0C) &= ~CLK_MMC_BIT;
    delay_long();

    /* sharepin: ePIN_AS_SDMMC1 (DATA pins only) */
    SYSCTRL(0x78) &= ~(1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    delay();

    OUT[6] = SYSCTRL(0x78);  /* sharepin CON1 */
    OUT[7] = SYSCTRL(0x74);  /* sharepin CON2 */

    /* PUPD config per sdhc_anyka.dll sub_82708A34 (MMC, result!=10 path) */
    SYSCTRL(0x9C) &= ~0xC0000000u;  /* PUPD1 clear bits 31:30 */
    SYSCTRL(0xA0) &= ~0x1C0u;       /* PUPD2 clear bits 8:7:6 */

    /* I/O control: SET BIT0 per driver (SYSCTRL+0xD4 |= 1) */
    SYSCTRL(0xD4) |= 1u;

    OUT[8]  = SYSCTRL(0x9C);  /* PUPD1 after */
    OUT[9]  = SYSCTRL(0xA0);  /* PUPD2 after */
    OUT[10] = SYSCTRL(0xD4);  /* I/O control after */

    /* MCI init per driver (0x0019FFFF = ENABLE|FAIL|PWRSAVE|CLK_EN|max_div) */
    MCI_CLOCK = 0;
    delay_long();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    delay_long();

    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | MCI_PWRSAVE | 0xFFFFu;
    delay_long();

    MCI_MASK      = 0xFFFFFFFFu;
    MCI(0x28)     = 512;           /* MCI_DATALENGTH per driver */
    delay();

    OUT[11] = MCI_CLOCK;
    OUT[12] = MCI_STA;

    /* CMD0 (GO_IDLE_STATE): CPSM_ENABLE only */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;  /* opcode 0 + CPSM_ENABLE */
    uint32_t t0 = wait_sta_any(STA_CMD_SENT | STA_RESP_TIMEO);

    OUT[13] = MCI_STA;       /* STA after CMD0 */
    OUT[14] = MCI_CMD;       /* CMD readback */
    OUT[15] = t0;            /* wait ticks */

    /* CMD8 (SEND_IF_COND): opcode 8 + CPSM_ENABLE + CPSM_RESPONSE */
    MCI_ARG = 0x1AAu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);
    uint32_t t8 = wait_sta_any(STA_CMD_SENT | STA_RESP_END | STA_RESP_TIMEO | STA_RESP_CRC);

    OUT[16] = MCI_STA;       /* STA after CMD8 */
    OUT[17] = MCI_CMD;       /* CMD readback */
    OUT[18] = t8;            /* wait ticks */
    OUT[19] = MCI(0x14);     /* RESP0 after CMD8 (if any) */
    OUT[20] = 0;
}
