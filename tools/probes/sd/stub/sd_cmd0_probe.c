/*
 * sd_cmd0_probe - send CMD0 (GO_IDLE_STATE) to SD card
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
#define MCI_RESP0     MCI(0x14)

#define STA_CMD_SENT  (1u << 5)
#define STA_RESP_END  (1u << 4)
#define STA_RESP_TO   (1u << 2)

#define WAIT_LIMIT    2000000u

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static uint32_t wait_reg_any(uint32_t addr, uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        if (REG32(addr) & mask)
            return i + 1u;
    }
    return 0;
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 32u; i++)
        OUT[i] = 0;
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x4D4D4330u; /* "MC0" = MMC CMD0 */
    OUT[1] = 1u;
    OUT[2] = MCI_BASE;

    /* clock gate + reset + sharepin */
    SYSCTRL(0x0C) &= ~(1u << 2);
    delay();
    SYSCTRL(0x10) |=  (1u << 18);
    delay();
    SYSCTRL(0x10) &= ~(1u << 18);
    delay();
    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 5)) | (2u << 5);
    delay();

    /* Start MMC clock: MCI_ENABLE | CLK_ENABLE | divider=240 (~200 kHz) */
    MCI_CLOCK = (1u << 20) | (1u << 19) | 240;
    delay();
    MCI_CLOCK = (1u << 20) | (1u << 19) | (1u << 16) | 240;
    delay();
    (void)MCI_STA;  /* clear any stale status */

    OUT[3] = MCI_CLOCK;
    OUT[4] = MCI_STA;

    /* CMD0: GO_IDLE_STATE (no response expected) */
    MCI_ARG = 0;
    MCI_CMD = 0;

    uint32_t wait_ticks = wait_reg_any(MCI_BASE + 0x34, STA_CMD_SENT);

    OUT[5] = wait_ticks;
    OUT[6] = MCI_STA;
    OUT[7] = 0;
}
