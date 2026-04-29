#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off) REG32(SYSCTRL_BASE + (off))

#define SPI_BASE 0x20024000u
#define SPI_CTRL REG32(SPI_BASE + 0x00)
#define SPI_STATUS REG32(SPI_BASE + 0x04)
#define SPI_COUNT REG32(SPI_BASE + 0x0C)
#define SPI_MODE_TX REG32(SPI_BASE + 0x10)
#define SPI_MODE_RX REG32(SPI_BASE + 0x14)
#define SPI_TXDATA REG32(SPI_BASE + 0x18)
#define SPI_RXDATA REG32(SPI_BASE + 0x1C)
#define SPI_CFG2 REG32(SPI_BASE + 0x20)

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

#define GPIO84_BIT (1u << 20)
#define GPIO84_SHAREPIN_BIT (1u << 8)
#define SPI_CLK_GATE_MASK 0x00000004u
#define SPI_SHAREPIN_CLK 0x40000000u
#define SPI_SHAREPIN_DATA_MASK 0x00000016u

#define WAIT_LIMIT 1000000u

enum {
    ATTEMPT_GPIO84_HIGH = 0,
    ATTEMPT_GPIO84_LOW = 1,
};

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 192u; i++)
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

static void record_state(uint32_t slot, uint32_t stage)
{
    volatile uint32_t *p = OUT + 8u + slot * 12u;

    p[0] = stage;
    p[1] = SYSCTRL(0x0C);
    p[2] = SYSCTRL(0x74);
    p[3] = SYSCTRL(0x78);
    p[4] = SYSCTRL(0x8C);
    p[5] = SYSCTRL(0x90);
    p[6] = SYSCTRL(0xC4);
    p[7] = SPI_CTRL;
    p[8] = SPI_STATUS;
    p[9] = SPI_COUNT;
    p[10] = SPI_CFG2;
    p[11] = 0;
}

static void record_read(uint32_t slot, uint32_t attempt, uint32_t reg,
                        uint32_t err, uint32_t rx, uint32_t wait_tx,
                        uint32_t wait_tx_done, uint32_t wait_rx_done)
{
    volatile uint32_t *p = OUT + 44u + slot * 12u;

    p[0] = attempt;
    p[1] = reg;
    p[2] = err;
    p[3] = rx;
    p[4] = wait_tx;
    p[5] = wait_tx_done;
    p[6] = wait_rx_done;
    p[7] = SPI_CTRL;
    p[8] = SPI_STATUS;
    p[9] = SPI_COUNT;
    p[10] = SYSCTRL(0x90);
    p[11] = SYSCTRL(0xC4);
}

static void gpio84_set(uint32_t high)
{
    SYSCTRL(0x74) &= ~GPIO84_SHAREPIN_BIT;
    SYSCTRL(0x8C) &= ~GPIO84_BIT;
    if (high)
        SYSCTRL(0x90) |= GPIO84_BIT;
    else
        SYSCTRL(0x90) &= ~GPIO84_BIT;
}

static void spi_enable(void)
{
    uint32_t share1 = SYSCTRL(0x78);

    gpio84_set(1);

    SYSCTRL(0x0C) &= ~SPI_CLK_GATE_MASK;
    share1 |= SPI_SHAREPIN_CLK;
    share1 &= ~SPI_SHAREPIN_DATA_MASK;
    SYSCTRL(0x78) = share1;

    SPI_CTRL = (0xFFu << 8) | 0x52u;
    SPI_CFG2 = 0x00FFFFFFu;
}

static uint32_t ch374_read_reg(uint32_t reg, uint32_t assert_gpio84, uint32_t *rx)
{
    uint32_t err = 0;
    uint32_t wait_tx;
    uint32_t wait_tx_done;
    uint32_t wait_rx_done;

    if (assert_gpio84)
        gpio84_set(0);
    else
        gpio84_set(1);
    delay();

    SPI_CTRL |= 0x20u;

    SPI_CTRL &= ~0x01u;
    SPI_CTRL |= 0x02u;
    SPI_MODE_TX = 0;
    SPI_COUNT = 2u;
    wait_tx = wait_reg_mask(SPI_BASE + 0x04u, 0x04u, 0x04u);
    if (wait_tx == 0)
        err |= 1u;
    SPI_TXDATA = (reg & 0xFFu) | (0xC0u << 8);
    wait_tx_done = wait_reg_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u);
    if (wait_tx_done == 0)
        err |= 2u;
    if (wait_reg_mask(SPI_BASE + 0x04u, 0x100u, 0x100u) == 0)
        err |= 4u;

    SPI_CTRL |= 0x01u;
    SPI_CTRL &= ~0x02u;
    SPI_MODE_RX = 0;
    SPI_COUNT = 1u;
    wait_rx_done = wait_reg_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u);
    if (wait_rx_done == 0)
        err |= 8u;

    *rx = SPI_RXDATA & 0xFFu;

    SPI_CTRL &= ~0x20u;
    gpio84_set(1);
    delay();

    OUT[4] = wait_tx;
    OUT[5] = wait_tx_done;
    OUT[6] = wait_rx_done;
    OUT[7] = err;

    return err;
}

static void read_reg_set(uint32_t attempt, uint32_t assert_gpio84, uint32_t base_slot)
{
    static const uint32_t regs[] = { 2u, 3u, 6u, 9u, 10u, 11u, 14u };

    for (uint32_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint32_t rx = 0;
        uint32_t err = ch374_read_reg(regs[i], assert_gpio84, &rx);
        record_read(base_slot + i, attempt, regs[i], err, rx, OUT[4], OUT[5], OUT[6]);
    }
}

void stub_main(void)
{
    clear_result();

    OUT[0] = 0x53504950u; /* "SPIP" */
    OUT[1] = 1u;
    OUT[2] = SPI_BASE;
    OUT[3] = GPIO84_BIT;

    record_state(0, 0);
    spi_enable();
    delay();
    record_state(1, 1);

    read_reg_set(ATTEMPT_GPIO84_HIGH, 0, 0);
    read_reg_set(ATTEMPT_GPIO84_LOW, 1, 7);

    record_state(2, 2);
}
