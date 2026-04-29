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

static uint32_t g_error;

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 20000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 160u; i++)
        OUT[i] = 0;
}

static uint32_t wait_reg_mask(uint32_t addr, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        if ((REG32(addr) & mask) == value)
            return i + 1u;
    }
    g_error |= 1u;
    return 0;
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

static void spi_cs_assert(void)
{
    gpio84_set(0);
    SPI_CTRL |= 0x20u;
}

static void spi_cs_deassert(void)
{
    SPI_CTRL &= ~0x20u;
    gpio84_set(1);
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

static void spi_tx(uint32_t byte_count, uint32_t word)
{
    SPI_CTRL &= ~0x01u;
    SPI_CTRL |= 0x02u;
    SPI_MODE_TX = 0;
    SPI_COUNT = byte_count;
    wait_reg_mask(SPI_BASE + 0x04u, 0x04u, 0x04u);
    SPI_TXDATA = word;
    wait_reg_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u);
    wait_reg_mask(SPI_BASE + 0x04u, 0x100u, 0x100u);
}

static uint32_t spi_rx1(void)
{
    SPI_CTRL |= 0x01u;
    SPI_CTRL &= ~0x02u;
    SPI_MODE_RX = 0;
    SPI_COUNT = 1u;
    wait_reg_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u);
    return SPI_RXDATA & 0xFFu;
}

static void ch374_reg_write(uint32_t reg, uint32_t value)
{
    spi_cs_assert();
    spi_tx(3u, (reg & 0xFFu) | (0x80u << 8) | ((value & 0xFFu) << 16));
    spi_cs_deassert();
}

static uint32_t ch374_reg_read(uint32_t reg)
{
    uint32_t value;

    spi_cs_assert();
    spi_tx(2u, (reg & 0xFFu) | (0xC0u << 8));
    value = spi_rx1();
    spi_cs_deassert();
    return value;
}

static uint32_t attach_mask(void)
{
    uint32_t reg2 = ch374_reg_read(2u);
    uint32_t reg3 = ch374_reg_read(3u);
    uint32_t mask = 0;

    if ((reg2 & 0x08u) != 0)
        mask |= 1u;
    if ((reg3 & 0x08u) != 0)
        mask |= 2u;
    if ((reg3 & 0x80u) != 0)
        mask |= 4u;
    return mask;
}

static void snapshot(uint32_t slot, uint32_t stage)
{
    volatile uint32_t *p = OUT + 8u + slot * 16u;

    p[0] = stage;
    p[1] = ch374_reg_read(2u);
    p[2] = ch374_reg_read(3u);
    p[3] = ch374_reg_read(5u);
    p[4] = ch374_reg_read(6u);
    p[5] = ch374_reg_read(7u);
    p[6] = ch374_reg_read(8u);
    p[7] = ch374_reg_read(9u);
    p[8] = ch374_reg_read(10u);
    p[9] = ch374_reg_read(11u);
    p[10] = ch374_reg_read(14u);
    p[11] = attach_mask();
    p[12] = SYSCTRL(0x90);
    p[13] = SYSCTRL(0xC4);
    p[14] = SPI_CTRL;
    p[15] = SPI_STATUS;
}

static void ch374_stage2(void)
{
    uint32_t reg2;

    ch374_reg_write(6u, 192u);
    reg2 = ch374_reg_read(2u);
    ch374_reg_write(2u, reg2 & 0x7Fu);
}

static void ch374_stage1(void)
{
    ch374_reg_write(6u, 0u);
    ch374_reg_write(8u, 0u);
    ch374_reg_write(14u, 0u);
    ch374_reg_write(9u, 31u);
    ch374_reg_write(7u, 3u);
    ch374_reg_write(5u, 64u);
    ch374_stage2();
}

void stub_main(void)
{
    g_error = 0;
    clear_result();

    OUT[0] = 0x43484154u; /* "CHAT" */
    OUT[1] = 1u;
    OUT[2] = SPI_BASE;
    OUT[3] = GPIO84_BIT;

    spi_enable();
    delay();
    snapshot(0u, 0u);

    ch374_stage1();
    delay();
    snapshot(1u, 1u);

    ch374_stage2();
    delay();
    snapshot(2u, 2u);

    OUT[4] = attach_mask();
    OUT[5] = g_error;
    OUT[6] = SPI_COUNT;
    OUT[7] = SYSCTRL(0xC4);
}
