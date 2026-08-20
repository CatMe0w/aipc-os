/*
 * Internal keyboard, a USB HID device behind a CH374 USB host bridge on SPI.
 *
 * The register numbers and the token sequence come from the EBOOT driver. See
 * docs/eboot/usb-hid-input.md. This driver enumerates the device at a fixed
 * address with a fixed interrupt endpoint and never reads its descriptors.
 *
 * This layer maps nothing. It queues the HID usage IDs that it reads, and each
 * image turns them into its own keys.
 */

#include <string.h>

#include "ch374.h"
#include "log.h"
#include "timer.h"

#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL(off)  REG32(0x08000000u + (off))

#define SPI_BASE      0x20024000u
#define SPI_CTRL      REG32(SPI_BASE + 0x00)
#define SPI_STATUS    REG32(SPI_BASE + 0x04)
#define SPI_COUNT     REG32(SPI_BASE + 0x0C)
#define SPI_MODE_TX   REG32(SPI_BASE + 0x10)
#define SPI_MODE_RX   REG32(SPI_BASE + 0x14)
#define SPI_TXDATA    REG32(SPI_BASE + 0x18)
#define SPI_RXDATA    REG32(SPI_BASE + 0x1C)
#define SPI_CFG2      REG32(SPI_BASE + 0x20)

#define SPI_CLK_GATE_MASK   0x00000004u
#define SHAREPIN1_SPI_CLK   0x40000000u
#define SHAREPIN1_SPI_DATA  0x00000006u
#define SHAREPIN1_WLED_PWM  0x00000010u

/* The chip select is GPIO84. This driver drives it by hand. */
#define CS_GPIO_BIT         (1u << 20)
#define CS_SHAREPIN_MASK    (1u << 8)

#define CH374_OK            20
#define CH374_ERR_NAK       42
#define CH374_ERR_TIMEOUT   250

#define CH374_PID_IN        9
#define CH374_PID_SETUP     13

#define USB_ADDR            2
#define HID_IN_EP           1
#define MAX_REPORTS_PER_POLL 4
#define QUEUE_SIZE          32
#define RETRY_DELAY_MS      1000u

static uint16_t s_queue[QUEUE_SIZE];
static unsigned int s_read_idx;
static unsigned int s_write_idx;
static uint8_t  s_prev_keys[6];
static uint8_t  s_prev_modifiers;
static uint8_t  s_in_toggle;
static uint32_t s_next_enumerate_ms;
static int      s_initialized;
static int      s_ready;

static void short_delay(uint32_t units)
{
    while (units-- != 0) {
        for (volatile uint32_t i = 0; i < 33u; i++)
            __asm__ volatile ("" : : : "memory");
    }
}

static int wait_mask(uint32_t addr, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((REG32(addr) & mask) == value)
            return 1;
    }
    return 0;
}

static void queue_push(int pressed, uint8_t usage)
{
    if (usage == 0)
        return;

    s_queue[s_write_idx] = (uint16_t)(((pressed != 0) << 8) | usage);
    s_write_idx = (s_write_idx + 1u) % QUEUE_SIZE;
    if (s_write_idx == s_read_idx)
        s_read_idx = (s_read_idx + 1u) % QUEUE_SIZE;
}

static void cs_set(int high)
{
    SYSCTRL(0x74) &= ~CS_SHAREPIN_MASK;
    SYSCTRL(0x8C) &= ~CS_GPIO_BIT;
    if (high)
        SYSCTRL(0x90) |= CS_GPIO_BIT;
    else
        SYSCTRL(0x90) &= ~CS_GPIO_BIT;
}

static void cs_assert(void)
{
    cs_set(0);
    SPI_CTRL |= 0x20u;
}

static void cs_deassert(void)
{
    SPI_CTRL &= ~0x20u;
    cs_set(1);
}

static void spi_enable(void)
{
    uint32_t share1 = SYSCTRL(0x78);

    cs_set(1);
    SYSCTRL(0x0C) &= ~SPI_CLK_GATE_MASK;
    share1 |= SHAREPIN1_SPI_CLK;
    share1 |= SHAREPIN1_WLED_PWM;
    share1 &= ~SHAREPIN1_SPI_DATA;
    SYSCTRL(0x78) = share1;

    SPI_CTRL = (0xFFu << 8) | 0x52u;
    SPI_CFG2 = 0x00FFFFFFu;
}

static int spi_tx_words(uint32_t count, uint32_t w0, uint32_t w1)
{
    SPI_CTRL &= ~0x01u;
    SPI_CTRL |= 0x02u;
    SPI_MODE_TX = 0;
    SPI_COUNT = count;

    if (!wait_mask(SPI_BASE + 0x04u, 0x04u, 0x04u))
        return 0;
    SPI_TXDATA = w0;

    if (count > 4u) {
        if (!wait_mask(SPI_BASE + 0x04u, 0x04u, 0x04u))
            return 0;
        SPI_TXDATA = w1;
    }

    if (!wait_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u))
        return 0;
    return wait_mask(SPI_BASE + 0x04u, 0x100u, 0x100u);
}

static void store_rx_bytes(uint8_t *dst, uint32_t word, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        dst[i] = (uint8_t)(word >> (i * 8u));
}

static int spi_rx_bytes(uint32_t count, uint8_t *dst)
{
    uint32_t words = count >> 2;
    uint32_t rem = count & 3u;

    SPI_CTRL |= 0x01u;
    SPI_CTRL &= ~0x02u;
    SPI_MODE_RX = 0;
    SPI_COUNT = count;

    for (uint32_t i = 0; i < words; i++) {
        if (!wait_mask(SPI_BASE + 0x04u, 0x40u, 0x40u))
            return 0;
        store_rx_bytes(dst, SPI_RXDATA, 4u);
        dst += 4;
    }

    if (!wait_mask(SPI_BASE + 0x0Cu, 0xFFFFu, 0u))
        return 0;

    if (rem != 0)
        store_rx_bytes(dst, SPI_RXDATA, rem);

    return 1;
}

static void ch374_reg_write(uint8_t reg, uint8_t value)
{
    cs_assert();
    (void)spi_tx_words(3u, (uint32_t)reg | (0x80u << 8) | ((uint32_t)value << 16), 0u);
    cs_deassert();
}

static uint8_t ch374_reg_read(uint8_t reg)
{
    uint8_t value = 0;

    cs_assert();
    if (spi_tx_words(2u, (uint32_t)reg | (0xC0u << 8), 0u))
        (void)spi_rx_bytes(1u, &value);
    cs_deassert();
    return value;
}

static void ch374_set_data8(uint32_t w0, uint32_t w1)
{
    cs_assert();
    (void)spi_tx_words(2u, 64u | (0x80u << 8), 0u);
    (void)spi_tx_words(8u, w0, w1);
    cs_deassert();
}

static int ch374_read_buffer(uint8_t len, uint8_t *buf)
{
    int ok;

    cs_assert();
    ok = spi_tx_words(2u, 0xC0u | (0xC0u << 8), 0u);
    if (ok)
        ok = spi_rx_bytes(len, buf);
    cs_deassert();
    return ok;
}

static uint8_t ch374_wait_irq(void)
{
    for (uint32_t i = 0; i < 500u; i++) {
        uint8_t r9 = ch374_reg_read(9);
        if ((r9 & 0x0Fu) != 0)
            return r9;
    }
    return 0;
}

static int ch374_issue_token(uint8_t endpoint, uint8_t pid, int toggle)
{
    uint8_t r9;
    uint8_t r10;
    uint8_t code;

    ch374_reg_write(13, (uint8_t)((endpoint & 0x0Fu) | (pid << 4)));
    ch374_reg_write(14, (uint8_t)(toggle ? 0xC8u : 0x08u));
    short_delay(200u);

    r9 = ch374_wait_irq();
    if (r9 == 0)
        return CH374_ERR_TIMEOUT;
    if ((r9 & 1u) == 0) {
        ch374_reg_write(9, 31u);
        return CH374_ERR_TIMEOUT;
    }

    ch374_reg_write(9, 17u);
    r10 = ch374_reg_read(10);
    code = r10 & 0x0Fu;

    if (pid == CH374_PID_SETUP)
        return code == 2u ? CH374_OK : code;

    if ((r10 & 7u) == 3u && (r10 & 0x10u) != 0)
        return CH374_OK;
    if (code == 14u || code == 10u || (r10 & 3u) != 0)
        return (int)(code | 0x20u);

    return CH374_ERR_NAK;
}

static int ch374_issue_token_retry(uint8_t endpoint, uint8_t pid, int toggle,
                                   uint32_t budget)
{
    for (uint32_t retry = 0; retry < budget; retry++) {
        int ret = ch374_issue_token(endpoint, pid, toggle);

        if (ret != CH374_ERR_NAK)
            return ret;
        timer_delay_ms(1u);
    }

    return CH374_ERR_NAK;
}

static int ch374_control_status_in(uint32_t setup_w0, uint32_t setup_w1)
{
    int ret;

    ch374_set_data8(setup_w0, setup_w1);
    ch374_reg_write(11, 8u);
    short_delay(200u);

    ret = ch374_issue_token(0, CH374_PID_SETUP, 0);
    if (ret != CH374_OK)
        return ret;

    ch374_reg_write(11, 0u);
    short_delay(200u);
    return ch374_issue_token_retry(0, CH374_PID_IN, 1, 20u);
}

static void ch374_stage2(void)
{
    uint8_t reg2;

    ch374_reg_write(6, 192u);
    reg2 = ch374_reg_read(2);
    ch374_reg_write(2, (uint8_t)(reg2 & 0x7Fu));
}

static void ch374_stage1(void)
{
    ch374_reg_write(6, 0u);
    ch374_reg_write(8, 0u);
    ch374_reg_write(14, 0u);
    ch374_reg_write(9, 31u);
    ch374_reg_write(7, 3u);
    ch374_reg_write(5, 64u);
    ch374_stage2();
}

static void ch374_port0_reset_enable(void)
{
    uint8_t reg2;

    ch374_reg_write(14, 0u);
    ch374_reg_write(8, 0u);
    reg2 = ch374_reg_read(2);
    ch374_reg_write(2, (uint8_t)((reg2 & 0xF9u) | 0x02u));
    timer_delay_ms(20u);
    ch374_reg_write(2, (uint8_t)(ch374_reg_read(2) & 0xFDu));
    timer_delay_ms(1u);
    ch374_reg_write(9, 22u);
    ch374_reg_write(2, (uint8_t)(ch374_reg_read(2) | 0x01u));
    ch374_reg_write(2, (uint8_t)(ch374_reg_read(2) | 0x04u));
    ch374_reg_write(6, (uint8_t)(ch374_reg_read(6) | 0x60u));
    timer_delay_ms(20u);
}

static int ch374_enumerate(void)
{
    int ret;

    s_ready = 0;
    s_in_toggle = 0;
    memset(s_prev_keys, 0, sizeof(s_prev_keys));

    ch374_stage1();
    ch374_stage2();
    ch374_port0_reset_enable();

    if ((ch374_reg_read(2) & 0x08u) == 0) {
        log_puts("ch374: no device on port0\n");
        return 0;
    }

    ret = ch374_control_status_in(0x00020500u, 0u);   /* SET_ADDRESS 2 */
    if (ret != CH374_OK) {
        log_puts("ch374: SET_ADDRESS failed\n");
        return 0;
    }

    timer_delay_ms(2u);
    ch374_reg_write(8, USB_ADDR);

    ret = ch374_control_status_in(0x00010900u, 0u);   /* SET_CONFIGURATION 1 */
    if (ret != CH374_OK) {
        log_puts("ch374: SET_CONFIGURATION failed\n");
        return 0;
    }

    s_ready = 1;
    log_puts("ch374: ready\n");
    return 1;
}

static int report_contains(const uint8_t keys[6], uint8_t usage)
{
    for (size_t i = 0; i < 6; i++) {
        if (keys[i] == usage)
            return 1;
    }
    return 0;
}

/* Byte 0 is the modifier bitmap, byte 1 is reserved, bytes 2..7 are the six
 * usage slots. Each modifier has its own usage ID, 0xE0 plus the bit number. */
static void process_report(const uint8_t *report)
{
    uint8_t modifiers = report[0];
    const uint8_t *keys = &report[2];

    for (unsigned int bit = 0; bit < 8u; bit++) {
        uint8_t mask = (uint8_t)(1u << bit);
        uint8_t usage = (uint8_t)(CH374_USAGE_MODIFIER_BASE + bit);

        if ((s_prev_modifiers & mask) == 0 && (modifiers & mask) != 0)
            queue_push(1, usage);
        else if ((s_prev_modifiers & mask) != 0 && (modifiers & mask) == 0)
            queue_push(0, usage);
    }

    for (size_t i = 0; i < sizeof(s_prev_keys); i++) {
        uint8_t usage = s_prev_keys[i];

        if (usage != 0 && !report_contains(keys, usage))
            queue_push(0, usage);
    }

    for (size_t i = 0; i < 6; i++) {
        uint8_t usage = keys[i];

        if (usage != 0 && !report_contains(s_prev_keys, usage))
            queue_push(1, usage);
    }

    memcpy(s_prev_keys, keys, sizeof(s_prev_keys));
    s_prev_modifiers = modifiers;
}

static int read_report(uint8_t *report, uint8_t *report_len)
{
    int ret = ch374_issue_token(HID_IN_EP, CH374_PID_IN, s_in_toggle);

    if (ret != CH374_OK)
        return ret;

    *report_len = ch374_reg_read(11);
    if (*report_len > 8u)
        *report_len = 8u;
    memset(report, 0, 8u);
    if (!ch374_read_buffer(*report_len, report))
        return CH374_ERR_TIMEOUT;

    s_in_toggle = (uint8_t)(s_in_toggle == 0);
    return CH374_OK;
}

void ch374_kbd_init(void)
{
    memset(s_queue, 0, sizeof(s_queue));
    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    s_read_idx = 0;
    s_write_idx = 0;
    s_prev_modifiers = 0;
    s_in_toggle = 0;

    spi_enable();
    cs_deassert();
    s_initialized = 1;

    if (!ch374_enumerate())
        s_next_enumerate_ms = timer_ms() + RETRY_DELAY_MS;
}

int ch374_kbd_ready(void)
{
    return s_ready;
}

void ch374_kbd_poll(void)
{
    if (!s_initialized)
        return;

    if (!s_ready) {
        if (timer_ms() >= s_next_enumerate_ms) {
            if (!ch374_enumerate())
                s_next_enumerate_ms = timer_ms() + RETRY_DELAY_MS;
        }
        return;
    }

    ch374_reg_write(8, USB_ADDR);

    for (uint32_t i = 0; i < MAX_REPORTS_PER_POLL; i++) {
        uint8_t report[8];
        uint8_t report_len = 0;
        int ret = read_report(report, &report_len);

        if (ret == CH374_OK && report_len >= 8u) {
            process_report(report);
            continue;
        }

        if (ret != CH374_ERR_NAK) {
            log_puts("ch374: lost device\n");
            s_ready = 0;
            s_next_enumerate_ms = timer_ms() + RETRY_DELAY_MS;
        }
        break;
    }
}

int ch374_kbd_pop(uint8_t *usage, int *pressed)
{
    if (s_read_idx == s_write_idx)
        return 0;

    uint16_t packed = s_queue[s_read_idx];

    s_read_idx = (s_read_idx + 1u) % QUEUE_SIZE;
    *pressed = (packed >> 8) & 1u;
    *usage = (uint8_t)(packed & 0xFFu);
    return 1;
}
