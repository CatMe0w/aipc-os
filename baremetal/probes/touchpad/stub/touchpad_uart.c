#define PROBE_ACTIVE 1
#define PROBE_DIAGNOSTIC 0
#define PROBE_POLL 0
#define PROBE_INIT 1
#define stub_main gpio_stub_main
#include "touchpad_probe.c"
#undef stub_main

#define UART_THRESHOLD (UART1_BASE + 0x0cu)
#define L2_PATH 0x2002c084u
#define UART_RX_BASE 0x48001080u
#define UART_ENABLE (1u << 21)
#define UART_TIMEOUT (1u << 23)
#define UART_ODD (1u << 26)
#define UART_RESET ((1u << 28) | (1u << 29))
#define UART_CLEAR ((1u << 30) | (1u << 3) | (1u << 2) | (1u << 1))
#define UART_EVENTS 1024u
#ifdef UART_IRQ_CHECK
#define UART_IRQ_ENABLE ((1u << 28) | (1u << 22))
#else
#define UART_IRQ_ENABLE 0u
#endif

struct uart_event {
    uint32_t ticks, status, config, data, count;
};

struct uart_header {
    uint32_t magic;
    uint32_t version;
    uint32_t mode;
    uint32_t complete;
    uint32_t timer_hz;
    uint32_t baud;
    uint32_t pll;
    uint32_t asic_hz;
    uint32_t control_before;
    uint32_t control_set;
    uint32_t control_after;
    uint32_t status_before;
    uint32_t status_after;
    uint32_t config_before;
    uint32_t config_after;
    uint32_t threshold_before;
    uint32_t threshold_after;
    uint32_t l2_path_before;
    uint32_t l2_path_after;
    uint32_t mux_before;
    uint32_t mux_after;
    uint32_t direction_before;
    uint32_t direction_after;
    uint32_t output_before;
    uint32_t output_after;
    uint32_t send_status;
    uint32_t rx_bytes;
    uint32_t event_count;
    uint32_t event_dropped;
    uint32_t rx_errors;
    uint32_t rx_full;
    uint32_t rx_timeouts;
    uint32_t rx_batches;
    uint32_t last_status;
    uint32_t last_config;
    uint32_t init_failed_step;
    uint32_t disable_status;
    uint32_t disable_reply;
    uint32_t capture_ticks;
    uint32_t clock_gate;
    uint32_t control_readback;
    uint32_t l2_buffers_before;
    uint32_t l2_buffers_after;
    uint32_t gpio_reply[4];
    uint32_t query_count;
    uint32_t irq_before;
    uint32_t irq_active;
    uint32_t irq_after;
    uint32_t reserved[13];
};

struct uart_result {
    struct uart_header header;
    struct uart_event events[UART_EVENTS];
};

_Static_assert(sizeof(struct uart_result) == 20736u,
               "result layout must match decode_uart.py");

static volatile struct uart_result *const ur =
    (volatile struct uart_result *)0x32010000u;
static uint32_t rx_index;

static void uart_drain(void)
{
    uint32_t status = REG32(UART1_STATUS);
    uint32_t config, next, words;

    /* The index can change before the receive status becomes active. */
    if (!(status & ((1u << 30) | (1u << 2) | (1u << 1)))) {
        if (status & (1u << 3)) {
            ur->header.rx_errors++;
            REG32(UART1_STATUS) = UART_IRQ_ENABLE | (1u << 3);
        }
        return;
    }
    config = REG32(UART1_CFG);
    next = (config >> 13) & 31u;
    words = (next - rx_index) & 31u;

    ur->header.last_status = status;
    ur->header.last_config = config;
    ur->header.irq_active |= REG32(SYSCTRL_BASE + 0xccu);
    if (status & (1u << 3))
        ur->header.rx_errors++;
    if (status & (1u << 1)) {
        ur->header.rx_full++;
        if (!words)
            words = 32u;
    }
    if (status & (1u << 2))
        ur->header.rx_timeouts++;
    if (words > 1u)
        ur->header.rx_batches++;

    for (uint32_t i = 0; i < words; i++) {
        uint32_t count = 4u;
        uint32_t data = REG32(UART_RX_BASE + rx_index * 4u);
        uint32_t index = ur->header.event_count;

        if (i + 1u == words && (status & (1u << 2))) {
            uint32_t fraction = (config >> 23) & 3u;
            if (fraction)
                count = fraction;
        }
        ur->header.rx_bytes += count;
        if (index < UART_EVENTS) {
            ur->events[index].ticks = elapsed_ticks();
            ur->events[index].status = status;
            ur->events[index].config = config;
            ur->events[index].data = data;
            ur->events[index].count = count;
            ur->header.event_count = index + 1u;
        } else {
            ur->header.event_dropped++;
        }
        rx_index = (rx_index + 1u) & 31u;
    }
    if (status & UART_CLEAR)
        REG32(UART1_STATUS) = UART_IRQ_ENABLE | (status & UART_CLEAR);
    ur->header.irq_after |= REG32(SYSCTRL_BASE + 0xccu);
}

void stub_main(void)
{
    static const uint8_t commands[] = {
        0xf3, 0xc8, 0xf3, 0x64, 0xf3, 0x50, 0xf2,
        0xf3, 0x0a, 0xf2, 0xe8, 0x03, 0xe6, 0xf3, 0x14,
    };
    struct pin_state pins;
    uint32_t pll, div, asic, half_div, control, start;
    volatile uint32_t *p = (volatile uint32_t *)ur;

    for (uint32_t i = 0; i < sizeof(*ur) / 4u; i++)
        p[i] = 0;
    clear_result();
    result->poll_send_failures = 0xffffffffu;
    snapshot_before();
    timer_start();
    last_levels = raw_to_levels(REG32(GPIO_IN1));
    ur->header.magic = 0x31555054u;
    ur->header.version = 1u;
    ur->header.mode = UART_MOTION;
    ur->header.timer_hz = TIMER_HZ;
    ur->header.baud = 13600u;
    ur->header.init_failed_step = 0xffffffffu;
    ur->header.clock_gate = REG32(SYSCTRL_BASE + 0x0cu);
    if (!timer_running) {
        ur->header.complete = 2u;
        return;
    }
    pins = configure_gpio_lines();
    ur->header.mux_before = pins.sharepin_con1;
    ur->header.direction_before = pins.dir1;
    ur->header.output_before = pins.out1;
    ur->header.control_before = REG32(UART1_CTRL);
    ur->header.status_before = REG32(UART1_STATUS);
    ur->header.config_before = REG32(UART1_CFG);
    ur->header.threshold_before = REG32(UART_THRESHOLD);
    ur->header.l2_path_before = REG32(L2_PATH);
    ur->header.l2_buffers_before = REG32(0x2002c08cu);

    if (UART_MOTION) {
        if (!init_command(0xffu, 0u, 2u, TIMER_HZ))
            goto finish;
        for (uint32_t i = 0; i < sizeof(commands); i++) {
            if (!init_command(commands[i], i + 1u,
                              commands[i] == 0xf2 ? 1u : 0u, IO_TIMEOUT_TICKS))
                goto finish;
        }
    } else {
        if (!init_command(0xf5u, 0u, 0u, IO_TIMEOUT_TICKS))
            goto finish;
        if (ps2_send(0xe9u) != 0u)
            goto finish;
        for (uint32_t i = 0; i < 4u; i++) {
            uint32_t reply = store_rx(RX_PHASE_STATUS_DATA, IO_TIMEOUT_TICKS);
            ur->header.gpio_reply[i] = reply;
            if (reply == 0xffffffffu || !(reply & RX_VALID))
                goto finish;
        }
    }

    pll = REG32(SYSCTRL_BASE + 4u);
    div = (pll >> 6) & 7u;
    asic = 4000000u * (62u + (pll & 63u));
    asic /= 1u + ((pll >> 17) & 15u);
    asic /= div ? (1u << div) : 2u;
    half_div = (asic * 4u + 13600u) / 27200u;
    control = ((half_div / 2u - 1u) & 0xffffu) |
              UART_ODD | UART_TIMEOUT | UART_ENABLE;
    if (half_div & 1u)
        control |= 1u << 22;
    ur->header.pll = pll;
    ur->header.asic_hz = asic;
    ur->header.control_set = control;

    REG32(UART1_CTRL) = control & ~UART_ENABLE;
    REG32(L2_PATH) = ur->header.l2_path_before | 0x30000000u;
    REG32(UART1_CTRL) = control | UART_RESET;
    REG32(UART_THRESHOLD) = 0u;
    REG32(UART1_STATUS) = UART_IRQ_ENABLE | UART_CLEAR;
    ur->header.irq_before = REG32(SYSCTRL_BASE + 0xccu);
    ur->header.control_readback = REG32(UART1_CTRL);
    rx_index = (REG32(UART1_CFG) >> 13) & 31u;

    for (uint32_t q = 0; q < (UART_MOTION ? 1u : 8u); q++) {
        ur->header.send_status = ps2_send(UART_MOTION ? 0xf4u : 0xe9u);
        if (ur->header.send_status != 0u)
            break;
        line_low(CLOCK_MASK);
        REG32(SHAREPIN_CON1) |= UART1_MUX_MASK;
        start = timer_raw();
        while (elapsed_from(start) < (UART_MOTION ? CAPTURE_TICKS : TIMER_HZ / 5u))
            uart_drain();
        ur->header.capture_ticks += elapsed_from(start);
        REG32(SHAREPIN_CON1) &= ~UART1_MUX_MASK;
        line_release(CLOCK_MASK);
        ur->header.query_count++;
    }
    REG32(SHAREPIN_CON1) &= ~UART1_MUX_MASK;
    REG32(UART1_CTRL) = control & ~UART_ENABLE;
    REG32(UART1_STATUS) = UART_CLEAR;
    REG32(UART_THRESHOLD) = ur->header.threshold_before;
    REG32(UART1_CTRL) = ur->header.control_before;
    REG32(L2_PATH) = ur->header.l2_path_before;

finish:
    ur->header.init_failed_step = result->poll_send_failures;
    ur->header.disable_status = ps2_send(0xf5u);
    if (!ur->header.disable_status)
        ur->header.disable_reply = store_rx(RX_PHASE_POST_DISABLE, IO_TIMEOUT_TICKS);
    restore_gpio_lines(&pins);
    ur->header.control_after = REG32(UART1_CTRL);
    ur->header.status_after = REG32(UART1_STATUS);
    ur->header.config_after = REG32(UART1_CFG);
    ur->header.threshold_after = REG32(UART_THRESHOLD);
    ur->header.l2_path_after = REG32(L2_PATH);
    ur->header.mux_after = REG32(SHAREPIN_CON1);
    ur->header.direction_after = REG32(GPIO_DIR1);
    ur->header.output_after = REG32(GPIO_OUT1);
    ur->header.l2_buffers_after = REG32(0x2002c08cu);
    REG32(TIMER2_CTRL) = 0u;
    ur->header.complete = 1u;
}
