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

#define CH374_PID_IN 9u
#define CH374_PID_SETUP 13u

#define SAMPLE_COUNT 24u
#define SAMPLE_BASE 48u
#define SAMPLE_WORDS 4u

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
    for (uint32_t i = 0; i < SAMPLE_BASE + SAMPLE_COUNT * SAMPLE_WORDS; i++)
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

static void read_buffer(uint32_t len, uint32_t *dst)
{
    cs_low();
    tx_words(2u, 0xC0u | (0xC0u << 8), 0u);
    rx_bytes(len, dst, 0u);
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

static uint32_t issue_token(uint32_t endpoint, uint32_t pid, uint32_t toggle)
{
    uint32_t r9;
    uint32_t r10;
    uint32_t code;

    reg_write(13u, (endpoint & 0x0Fu) | (pid << 4));
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

    if (pid == CH374_PID_SETUP)
        return code == 2u ? CH374_OK : code;
    if ((r10 & 7u) == 3u && (r10 & 0x10u) != 0)
        return CH374_OK;
    if (code == 14u || code == 10u || (r10 & 3u) != 0)
        return code | 0x20u;
    return CH374_ERR_NAK;
}

static uint32_t control_status_in(uint32_t w0, uint32_t w1, uint32_t base)
{
    uint32_t ret;

    set_addr_data8(w0, w1);
    reg_write(11u, 8u);
    delay_short(200u);
    ret = issue_token(0u, CH374_PID_SETUP, 0u);
    OUT[base + 0u] = ret;
    OUT[base + 1u] = g_irq;
    OUT[base + 2u] = g_result;
    if (ret != CH374_OK)
        return ret;

    reg_write(11u, 0u);
    delay_short(200u);
    for (uint32_t retry = 0; retry < 20u; retry++) {
        ret = issue_token(0u, CH374_PID_IN, 1u);
        OUT[base + 4u] = ret;
        OUT[base + 5u] = g_irq;
        OUT[base + 6u] = g_result;
        OUT[base + 7u] = retry;
        if (ret != CH374_ERR_NAK)
            break;
        delay_ms(1u);
    }
    return ret;
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

static void sample_ep81(uint32_t index, uint32_t *toggle, uint32_t *nonzero_count)
{
    uint32_t ret = CH374_ERR_TIMEOUT;
    uint32_t len = 0;
    uint32_t report[2] = { 0, 0 };
    uint32_t retry = 0;
    uint32_t base = SAMPLE_BASE + index * SAMPLE_WORDS;

    for (retry = 0; retry < 24u; retry++) {
        ret = issue_token(1u, CH374_PID_IN, *toggle);
        if (ret != CH374_ERR_NAK)
            break;
        delay_ms(1u);
    }

    if (ret == CH374_OK) {
        len = reg_read(11u);
        if (len > 8u)
            len = 8u;
        read_buffer(len, report);
        *toggle ^= 1u;
        if (report[0] != 0 || report[1] != 0)
            (*nonzero_count)++;
    }

    OUT[base + 0u] = ret;
    OUT[base + 1u] = (retry & 0xFFu) |
        ((len & 0xFFu) << 8) |
        ((g_irq & 0xFFu) << 16) |
        ((g_result & 0xFFu) << 24);
    OUT[base + 2u] = report[0];
    OUT[base + 3u] = report[1];
}

void stub_main(void)
{
    uint32_t toggle = 0;
    uint32_t nonzero_count = 0;

    g_error = 0;
    g_irq = 0;
    g_result = 0;
    clear_result();

    OUT[0] = 0x43533831u; /* "C8S1" */
    OUT[1] = SAMPLE_COUNT;
    OUT[2] = SPI_BASE;
    OUT[3] = GPIO84_BIT;

    spi_enable();
    stage1();
    stage2();
    port0_reset_enable();

    OUT[4] = control_status_in(0x00020500u, 0u, 16u);
    delay_ms(2u);
    reg_write(8u, 2u);
    if (OUT[4] == CH374_OK)
        OUT[5] = control_status_in(0x00010900u, 0u, 32u);

    if (OUT[5] == CH374_OK) {
        for (uint32_t i = 0; i < SAMPLE_COUNT; i++) {
            sample_ep81(i, &toggle, &nonzero_count);
            delay_ms(10u);
        }
    }

    OUT[6] = nonzero_count;
    OUT[7] = toggle;
    OUT[8] = g_error;
    OUT[9] = reg_read(11u);
    OUT[10] = reg_read(9u);
    OUT[11] = reg_read(10u);
    OUT[12] = reg_read(14u);
    OUT[13] = SYSCTRL(0xC4);
}
