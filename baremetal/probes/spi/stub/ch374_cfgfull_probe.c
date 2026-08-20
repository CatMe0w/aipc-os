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

#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)

#define GPIO84_BIT (1u << 20)
#define GPIO84_SHAREPIN_BIT (1u << 8)
#define SPI_CLK_GATE_MASK 0x00000004u
#define SPI_SHAREPIN_CLK 0x40000000u
#define SPI_SHAREPIN_DATA_MASK 0x00000016u

#define CH374_OK 20u
#define CH374_ERR_NAK 42u
#define CH374_ERR_TIMEOUT 250u

#define CH374_PID_OUT 1u
#define CH374_PID_IN 9u
#define CH374_PID_SETUP 13u

static uint32_t g_error;
static uint32_t g_irq;
static uint32_t g_result;

static void delay_short(uint32_t units)
{
    while (units-- != 0) {
        for (volatile uint32_t i = 0; i < 33u; i++)
            __asm__ volatile ("" : : : "memory");
    }
}

static void delay_ms(uint32_t ms)
{
    while (ms-- != 0) {
        for (volatile uint32_t i = 0; i < 20000u; i++)
            __asm__ volatile ("" : : : "memory");
    }
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 96u; i++)
        OUT[i] = 0;
}

static uint32_t wait_mask(uint32_t addr, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((REG32(addr) & mask) == value)
            return i + 1u;
    }
    g_error |= 1u;
    return 0;
}

static void gpio84(uint32_t high)
{
    SYSCTRL(0x74) &= ~GPIO84_SHAREPIN_BIT;
    SYSCTRL(0x8C) &= ~GPIO84_BIT;
    if (high)
        SYSCTRL(0x90) |= GPIO84_BIT;
    else
        SYSCTRL(0x90) &= ~GPIO84_BIT;
}

static void cs_low(void)
{
    gpio84(0);
    SPI_CTRL |= 0x20u;
}

static void cs_high(void)
{
    SPI_CTRL &= ~0x20u;
    gpio84(1);
}

static void spi_enable(void)
{
    uint32_t share1 = SYSCTRL(0x78);

    gpio84(1);
    SYSCTRL(0x0C) &= ~SPI_CLK_GATE_MASK;
    share1 |= SPI_SHAREPIN_CLK;
    share1 &= ~SPI_SHAREPIN_DATA_MASK;
    SYSCTRL(0x78) = share1;
    SPI_CTRL = (0xFFu << 8) | 0x52u;
    SPI_CFG2 = 0x00FFFFFFu;
}

static void tx_words(uint32_t count, uint32_t w0, uint32_t w1)
{
    SPI_CTRL &= ~0x01u;
    SPI_CTRL |= 0x02u;
    SPI_MODE_TX = 0;
    SPI_COUNT = count;
    wait_mask(SPI_BASE + 0x04u, 0x04u, 0x04u);
    SPI_TXDATA = w0;
    if (count > 4u) {
        wait_mask(SPI_BASE + 0x04u, 0x04u, 0x04u);
        SPI_TXDATA = w1;
    }
    wait_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u);
    wait_mask(SPI_BASE + 0x04u, 0x100u, 0x100u);
}

static void store_bytes(uint32_t *dst, uint32_t byte_off, uint32_t word, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = byte_off + i;
        uint32_t idx = off >> 2;
        uint32_t shift = (off & 3u) * 8u;
        uint32_t mask = 0xFFu << shift;
        uint32_t b = ((word >> (i * 8u)) & 0xFFu) << shift;

        dst[idx] = (dst[idx] & ~mask) | b;
    }
}

static void rx_bytes(uint32_t count, uint32_t *dst, uint32_t byte_off)
{
    uint32_t off = byte_off;
    uint32_t words = count >> 2;
    uint32_t rem = count & 3u;

    SPI_CTRL |= 0x01u;
    SPI_CTRL &= ~0x02u;
    SPI_MODE_RX = 0;
    SPI_COUNT = count;
    for (uint32_t i = 0; i < words; i++) {
        wait_mask(SPI_BASE + 0x04u, 0x40u, 0x40u);
        store_bytes(dst, off, SPI_RXDATA, 4u);
        off += 4u;
    }
    wait_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u);
    if (rem != 0)
        store_bytes(dst, off, SPI_RXDATA, rem);
}

static void reg_write(uint32_t reg, uint32_t value)
{
    cs_low();
    tx_words(3u, (reg & 0xFFu) | (0x80u << 8) | ((value & 0xFFu) << 16), 0u);
    cs_high();
}

static uint32_t reg_read(uint32_t reg)
{
    uint32_t v = 0;

    cs_low();
    tx_words(2u, (reg & 0xFFu) | (0xC0u << 8), 0u);
    rx_bytes(1u, &v, 0u);
    cs_high();
    return v & 0xFFu;
}

static void set_addr_data8(uint32_t w0, uint32_t w1)
{
    cs_low();
    tx_words(2u, 64u | (0x80u << 8), 0u);
    tx_words(8u, w0, w1);
    cs_high();
}

static void read_buffer(uint32_t len, uint32_t *dst, uint32_t off)
{
    cs_low();
    tx_words(2u, 0xC0u | (0xC0u << 8), 0u);
    rx_bytes(len, dst, off);
    cs_high();
}

static uint32_t wait_irq(void)
{
    for (uint32_t i = 0; i < 500u; i++) {
        uint32_t r9 = reg_read(9u);
        if ((r9 & 0x0Fu) != 0)
            return r9;
    }
    return 0;
}

static uint32_t issue_token(uint32_t pid, uint32_t toggle)
{
    uint32_t r9;
    uint32_t r10;
    uint32_t code;

    reg_write(13u, pid << 4);
    reg_write(14u, toggle ? 0xC8u : 0x08u);
    delay_short(200u);
    r9 = wait_irq();
    g_irq = r9;
    if (r9 == 0)
        return CH374_ERR_TIMEOUT;
    if ((r9 & 1u) == 0) {
        reg_write(9u, 31u);
        return CH374_ERR_TIMEOUT;
    }

    reg_write(9u, 17u);
    r10 = reg_read(10u);
    g_result = r10;
    code = r10 & 0x0Fu;

    if (pid == CH374_PID_SETUP || pid == CH374_PID_OUT)
        return code == 2u ? CH374_OK : code;
    if ((r10 & 7u) == 3u && (r10 & 0x10u) != 0)
        return CH374_OK;
    if (code == 14u || code == 10u || (r10 & 3u) != 0)
        return code | 0x20u;
    return CH374_ERR_NAK;
}

static void stage2(void)
{
    uint32_t r2;

    reg_write(6u, 192u);
    r2 = reg_read(2u);
    reg_write(2u, r2 & 0x7Fu);
}

static void stage1(void)
{
    reg_write(6u, 0u);
    reg_write(8u, 0u);
    reg_write(14u, 0u);
    reg_write(9u, 31u);
    reg_write(7u, 3u);
    reg_write(5u, 64u);
    stage2();
}

static void port0_reset_enable(void)
{
    uint32_t r2;

    reg_write(14u, 0u);
    reg_write(8u, 0u);
    r2 = reg_read(2u);
    reg_write(2u, (r2 & 0xF9u) | 0x02u);
    delay_ms(20u);
    reg_write(2u, reg_read(2u) & 0xFDu);
    delay_ms(1u);
    reg_write(9u, 22u);
    reg_write(2u, reg_read(2u) | 0x01u);
    reg_write(2u, reg_read(2u) | 0x04u);
    reg_write(6u, reg_read(6u) | 0x60u);
    delay_ms(20u);
}

static uint32_t get_config_full(void)
{
    uint32_t requested = 59u;
    uint32_t got = 0;
    uint32_t toggle = 1u;
    uint32_t *desc = (uint32_t *)&OUT[64];
    uint32_t ret;

    for (uint32_t i = 0; i < 16u; i++)
        desc[i] = 0;

    set_addr_data8(0x02000680u, 0x003B0000u);
    reg_write(11u, 8u);
    delay_short(200u);
    ret = issue_token(CH374_PID_SETUP, 0u);
    OUT[16] = ret;
    OUT[17] = g_irq;
    OUT[18] = g_result;
    if (ret != CH374_OK)
        return got;

    for (uint32_t tries = 0; tries < 8u && requested != 0; tries++) {
        uint32_t chunk;

        for (uint32_t retry = 0; retry < 20u; retry++) {
            ret = issue_token(CH374_PID_IN, toggle);
            OUT[24 + tries * 4u + 0u] = ret;
            OUT[24 + tries * 4u + 1u] = g_irq;
            OUT[24 + tries * 4u + 2u] = g_result;
            OUT[24 + tries * 4u + 3u] = retry;
            if (ret != CH374_ERR_NAK)
                break;
            delay_ms(1u);
        }
        if (ret != CH374_OK)
            break;

        chunk = reg_read(11u);
        OUT[56 + tries] = chunk;
        if (chunk > requested)
            chunk = requested;
        read_buffer(chunk, desc, got);
        got += chunk;
        requested -= chunk;
        if (chunk == 0 || chunk < 8u)
            break;
        toggle = toggle == 0u;
    }

    reg_write(11u, 0u);
    OUT[20] = issue_token(CH374_PID_OUT, 1u);
    OUT[21] = g_irq;
    OUT[22] = g_result;
    return got;
}

void stub_main(void)
{
    g_error = 0;
    g_irq = 0;
    g_result = 0;
    clear_result();

    OUT[0] = 0x43484647u; /* "CHFG" */
    OUT[1] = 1u;
    OUT[2] = SPI_BASE;
    OUT[3] = GPIO84_BIT;

    spi_enable();
    stage1();
    stage2();
    port0_reset_enable();

    OUT[4] = reg_read(2u);
    OUT[5] = reg_read(3u);
    OUT[6] = reg_read(6u);
    OUT[7] = reg_read(9u);
    OUT[8] = get_config_full();
    OUT[9] = reg_read(11u);
    OUT[10] = g_error;
    OUT[11] = SYSCTRL(0xC4);
    OUT[12] = reg_read(2u);
    OUT[13] = reg_read(9u);
    OUT[14] = reg_read(10u);
    OUT[15] = reg_read(14u);
}
