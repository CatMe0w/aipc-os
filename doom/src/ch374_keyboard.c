#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "doomkeys.h"
#include "doomgeneric.h"

#include "ch374_keyboard.h"

#define REG32(addr)  (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE  0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define SPI_BASE      0x20024000u
#define SPI_CTRL      REG32(SPI_BASE + 0x00)
#define SPI_STATUS    REG32(SPI_BASE + 0x04)
#define SPI_COUNT     REG32(SPI_BASE + 0x0C)
#define SPI_MODE_TX   REG32(SPI_BASE + 0x10)
#define SPI_MODE_RX   REG32(SPI_BASE + 0x14)
#define SPI_TXDATA    REG32(SPI_BASE + 0x18)
#define SPI_RXDATA    REG32(SPI_BASE + 0x1C)
#define SPI_CFG2      REG32(SPI_BASE + 0x20)

#define SYSCTRL_SPI_CLK_GATE_MASK  0x00000004u
#define SYSCTRL_SHAREPIN1_SPI_CLK  0x40000000u
#define SYSCTRL_SHAREPIN1_SPI_DATA 0x00000006u
#define SYSCTRL_SHAREPIN1_WLED_PWM 0x00000010u

#define CH374_CS_GPIO_BIT          (1u << 20)
#define CH374_CS_SHAREPIN_MASK     (1u << 8)

#define CH374_OK             20
#define CH374_ERR_NAK        42
#define CH374_ERR_TIMEOUT    250

#define CH374_PID_IN         9
#define CH374_PID_SETUP      13

#define CH374_KEY_QUEUE_SIZE 64
#define CH374_USB_ADDR       2
#define CH374_HID_IN_EP      1
#define CH374_MAX_REPORTS_PER_POLL 4

static uint16_t s_key_queue[CH374_KEY_QUEUE_SIZE];
static unsigned int s_key_queue_read_idx;
static unsigned int s_key_queue_write_idx;
static uint8_t s_key_refcount[256];
static uint8_t s_prev_modifiers;
static uint8_t s_prev_keys[6];
static uint8_t s_hid_in_toggle;
static uint32_t s_next_enumerate_ms;
static int s_keyboard_initialized;
static int s_keyboard_ready;

static uint8_t ch374_reg_read(uint8_t reg);

static void short_delay(uint32_t units)
{
    while (units-- != 0) {
        for (volatile uint32_t i = 0; i < 33u; i++)
            __asm__ volatile ("" : : : "memory");
    }
}

static void delay_ms(uint32_t ms)
{
    if (ms != 0)
        DG_SleepMs(ms);
}

static int wait_mask(uint32_t addr, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((REG32(addr) & mask) == value)
            return 1;
    }
    return 0;
}

static void queue_key_event_raw(int pressed, unsigned char doom_key)
{
    uint16_t packed = (uint16_t)(((pressed != 0) << 8) | doom_key);

    s_key_queue[s_key_queue_write_idx] = packed;
    s_key_queue_write_idx = (s_key_queue_write_idx + 1u) % CH374_KEY_QUEUE_SIZE;
    if (s_key_queue_write_idx == s_key_queue_read_idx)
        s_key_queue_read_idx = (s_key_queue_read_idx + 1u) % CH374_KEY_QUEUE_SIZE;
}

static void emit_doom_key_event(int pressed, unsigned char doom_key)
{
    if (doom_key == 0)
        return;

    if (pressed) {
        if (s_key_refcount[doom_key] == 0)
            queue_key_event_raw(1, doom_key);
        if (s_key_refcount[doom_key] != 0xFFu)
            s_key_refcount[doom_key]++;
    } else {
        if (s_key_refcount[doom_key] == 0)
            return;
        s_key_refcount[doom_key]--;
        if (s_key_refcount[doom_key] == 0)
            queue_key_event_raw(0, doom_key);
    }
}

static unsigned char hid_modifier_to_doom_key(unsigned int bit)
{
    switch (bit) {
    case 0: return KEY_FIRE;
    case 1: return KEY_RSHIFT;
    case 2: return KEY_LALT;
    case 4: return KEY_FIRE;
    case 5: return KEY_RSHIFT;
    case 6: return KEY_RALT;
    default:
        return 0;
    }
}

static unsigned char hid_usage_to_doom_key(uint8_t usage)
{
    if (usage >= 0x04u && usage <= 0x1Du)
        return (unsigned char)('a' + (usage - 0x04u));

    if (usage >= 0x1Eu && usage <= 0x26u)
        return (unsigned char)('1' + (usage - 0x1Eu));

    switch (usage) {
    case 0x27: return '0';
    case 0x28: return KEY_ENTER;
    case 0x29: return KEY_ESCAPE;
    case 0x2A: return KEY_BACKSPACE;
    case 0x2B: return KEY_TAB;
    case 0x2C: return KEY_USE;
    case 0x2D: return KEY_MINUS;
    case 0x2E: return KEY_EQUALS;
    case 0x2F: return '[';
    case 0x30: return ']';
    case 0x31: return '\\';
    case 0x33: return ';';
    case 0x34: return '\'';
    case 0x35: return '`';
    case 0x36: return ',';
    case 0x37: return '.';
    case 0x38: return '/';
    case 0x39: return KEY_CAPSLOCK;
    case 0x3A: return KEY_F1;
    case 0x3B: return KEY_F2;
    case 0x3C: return KEY_F3;
    case 0x3D: return KEY_F4;
    case 0x3E: return KEY_F5;
    case 0x3F: return KEY_F6;
    case 0x40: return KEY_F7;
    case 0x41: return KEY_F8;
    case 0x42: return KEY_F9;
    case 0x43: return KEY_F10;
    case 0x44: return KEY_F11;
    case 0x45: return KEY_F12;
    case 0x49: return KEY_INS;
    case 0x4A: return KEY_HOME;
    case 0x4B: return KEY_PGUP;
    case 0x4C: return KEY_DEL;
    case 0x4D: return KEY_END;
    case 0x4E: return KEY_PGDN;
    case 0x4F: return KEY_RIGHTARROW;
    case 0x50: return KEY_LEFTARROW;
    case 0x51: return KEY_DOWNARROW;
    case 0x52: return KEY_UPARROW;
    default:
        return 0;
    }
}

static void gpio84_set(int high)
{
    SYSCTRL(0x74) &= ~CH374_CS_SHAREPIN_MASK;
    SYSCTRL(0x8C) &= ~CH374_CS_GPIO_BIT;
    if (high)
        SYSCTRL(0x90) |= CH374_CS_GPIO_BIT;
    else
        SYSCTRL(0x90) &= ~CH374_CS_GPIO_BIT;
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
    SYSCTRL(0x0C) &= ~SYSCTRL_SPI_CLK_GATE_MASK;
    share1 |= SYSCTRL_SHAREPIN1_SPI_CLK;
    share1 |= SYSCTRL_SHAREPIN1_WLED_PWM;
    share1 &= ~SYSCTRL_SHAREPIN1_SPI_DATA;
    SYSCTRL(0x78) = share1;

    /* This is the exact conservative mode used by the validated USB-boot stubs. */
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

static int ch374_reg_write_checked(uint8_t reg, uint8_t value)
{
    int ok;

    spi_cs_assert();
    ok = spi_tx_words(3u, (uint32_t)reg | (0x80u << 8) | ((uint32_t)value << 16), 0u);
    spi_cs_deassert();
    return ok;
}

static void ch374_reg_write(uint8_t reg, uint8_t value)
{
    (void)ch374_reg_write_checked(reg, value);
}

static uint8_t ch374_reg_read(uint8_t reg)
{
    uint8_t value = 0;

    spi_cs_assert();
    if (spi_tx_words(2u, (uint32_t)reg | (0xC0u << 8), 0u))
        (void)spi_rx_bytes(1u, &value);
    spi_cs_deassert();
    return value;
}

static void ch374_set_data8(uint32_t w0, uint32_t w1)
{
    spi_cs_assert();
    (void)spi_tx_words(2u, 64u | (0x80u << 8), 0u);
    (void)spi_tx_words(8u, w0, w1);
    spi_cs_deassert();
}

static int ch374_read_buffer(uint8_t len, uint8_t *buf)
{
    int ok;

    spi_cs_assert();
    ok = spi_tx_words(2u, 0xC0u | (0xC0u << 8), 0u);
    if (ok)
        ok = spi_rx_bytes(len, buf);
    spi_cs_deassert();
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

static int ch374_issue_token_retry(uint8_t endpoint, uint8_t pid, int toggle, uint32_t budget)
{
    for (uint32_t retry = 0; retry < budget; retry++) {
        int ret = ch374_issue_token(endpoint, pid, toggle);

        if (ret != CH374_ERR_NAK)
            return ret;
        delay_ms(1u);
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
    delay_ms(20u);
    ch374_reg_write(2, (uint8_t)(ch374_reg_read(2) & 0xFDu));
    delay_ms(1u);
    ch374_reg_write(9, 22u);
    ch374_reg_write(2, (uint8_t)(ch374_reg_read(2) | 0x01u));
    ch374_reg_write(2, (uint8_t)(ch374_reg_read(2) | 0x04u));
    ch374_reg_write(6, (uint8_t)(ch374_reg_read(6) | 0x60u));
    delay_ms(20u);
}

static int ch374_enumerate_keyboard(void)
{
    int ret;

    s_keyboard_ready = 0;
    s_hid_in_toggle = 0;
    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    s_prev_modifiers = 0;

    ch374_stage1();
    ch374_stage2();
    ch374_port0_reset_enable();

    if ((ch374_reg_read(2) & 0x08u) == 0) {
        printf("CH374: port0 has no attach bit r2=%02x r3=%02x\n",
               ch374_reg_read(2), ch374_reg_read(3));
        return 0;
    }

    ret = ch374_control_status_in(0x00020500u, 0u);
    if (ret != CH374_OK) {
        printf("CH374: SET_ADDRESS failed ret=%d r9=%02x r10=%02x\n",
               ret, ch374_reg_read(9), ch374_reg_read(10));
        return 0;
    }

    delay_ms(2u);
    ch374_reg_write(8, CH374_USB_ADDR);

    ret = ch374_control_status_in(0x00010900u, 0u);
    if (ret != CH374_OK) {
        printf("CH374: SET_CONFIGURATION failed ret=%d r9=%02x r10=%02x\n",
               ret, ch374_reg_read(9), ch374_reg_read(10));
        return 0;
    }

    s_keyboard_ready = 1;
    printf("CH374: keyboard ready on port0 addr=%u ep=0x81\n", CH374_USB_ADDR);
    return 1;
}

static int hid_report_contains_usage(const uint8_t keys[6], uint8_t usage)
{
    for (size_t i = 0; i < 6; i++) {
        if (keys[i] == usage)
            return 1;
    }
    return 0;
}

static void ch374_process_report(const uint8_t *report, uint8_t report_len)
{
    uint8_t modifiers = report_len > 0 ? report[0] : 0;
    const uint8_t *keys = report_len >= 8 ? &report[2] : NULL;

    for (unsigned int bit = 0; bit < 8u; bit++) {
        uint8_t mask = (uint8_t)(1u << bit);

        if ((s_prev_modifiers & mask) == 0 && (modifiers & mask) != 0)
            emit_doom_key_event(1, hid_modifier_to_doom_key(bit));
        else if ((s_prev_modifiers & mask) != 0 && (modifiers & mask) == 0)
            emit_doom_key_event(0, hid_modifier_to_doom_key(bit));
    }

    if (keys != NULL) {
        for (size_t i = 0; i < sizeof(s_prev_keys); i++) {
            uint8_t usage = s_prev_keys[i];

            if (usage != 0 && !hid_report_contains_usage(keys, usage))
                emit_doom_key_event(0, hid_usage_to_doom_key(usage));
        }

        for (size_t i = 0; i < 6; i++) {
            uint8_t usage = keys[i];

            if (usage != 0 && !hid_report_contains_usage(s_prev_keys, usage))
                emit_doom_key_event(1, hid_usage_to_doom_key(usage));
        }

        memcpy(s_prev_keys, keys, sizeof(s_prev_keys));
    } else {
        memset(s_prev_keys, 0, sizeof(s_prev_keys));
    }

    s_prev_modifiers = modifiers;
}

static int ch374_read_hid_report(uint8_t *report, uint8_t *report_len)
{
    int ret = ch374_issue_token(CH374_HID_IN_EP, CH374_PID_IN, s_hid_in_toggle);

    if (ret != CH374_OK)
        return ret;

    *report_len = ch374_reg_read(11);
    if (*report_len > 8u)
        *report_len = 8u;
    memset(report, 0, 8u);
    if (!ch374_read_buffer(*report_len, report))
        return CH374_ERR_TIMEOUT;

    s_hid_in_toggle = (uint8_t)(s_hid_in_toggle == 0);
    return CH374_OK;
}

void aipc_keyboard_init(void)
{
    memset(s_key_queue, 0, sizeof(s_key_queue));
    memset(s_key_refcount, 0, sizeof(s_key_refcount));
    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    s_key_queue_read_idx = 0;
    s_key_queue_write_idx = 0;
    s_prev_modifiers = 0;
    s_hid_in_toggle = 0;
    s_next_enumerate_ms = 0;

    spi_enable();
    spi_cs_deassert();

    s_keyboard_initialized = 1;
    printf("CH374: keyboard host initialized on hardware SPI @ 0x%08x, GPIO84 CS#\n",
           SPI_BASE);

    if (!ch374_enumerate_keyboard())
        s_next_enumerate_ms = DG_GetTicksMs() + 1000u;
}

void aipc_keyboard_poll(void)
{
    uint32_t now;

    if (!s_keyboard_initialized)
        return;

    now = DG_GetTicksMs();
    if (!s_keyboard_ready) {
        if (now >= s_next_enumerate_ms) {
            if (!ch374_enumerate_keyboard())
                s_next_enumerate_ms = DG_GetTicksMs() + 1000u;
        }
        return;
    }

    ch374_reg_write(8, CH374_USB_ADDR);

    for (uint32_t i = 0; i < CH374_MAX_REPORTS_PER_POLL; i++) {
        uint8_t report[8];
        uint8_t report_len = 0;
        int ret = ch374_read_hid_report(report, &report_len);

        if (ret == CH374_OK && report_len >= 8u) {
            ch374_process_report(report, report_len);
            continue;
        }

        if (ret != CH374_ERR_NAK) {
            printf("CH374: report read failed ret=%d r2=%02x r9=%02x r10=%02x\n",
                   ret, ch374_reg_read(2), ch374_reg_read(9), ch374_reg_read(10));
            s_keyboard_ready = 0;
            s_next_enumerate_ms = DG_GetTicksMs() + 1000u;
        }
        break;
    }
}

int aipc_keyboard_get_event(int *pressed, unsigned char *key)
{
    if (s_key_queue_read_idx == s_key_queue_write_idx)
        aipc_keyboard_poll();

    if (s_key_queue_read_idx == s_key_queue_write_idx)
        return 0;

    {
        uint16_t packed = s_key_queue[s_key_queue_read_idx];

        s_key_queue_read_idx = (s_key_queue_read_idx + 1u) % CH374_KEY_QUEUE_SIZE;
        *pressed = (packed >> 8) & 1u;
        *key = (unsigned char)(packed & 0xFFu);
    }

    return 1;
}
