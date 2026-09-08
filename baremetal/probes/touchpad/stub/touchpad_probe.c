#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE          0x08000000u
#define SHAREPIN_CON2         (SYSCTRL_BASE + 0x74u)
#define SHAREPIN_CON1         (SYSCTRL_BASE + 0x78u)
#define GPIO_DIR1             (SYSCTRL_BASE + 0x7cu)
#define GPIO_OUT1             (SYSCTRL_BASE + 0x80u)
#define GPIO_PULL1            (SYSCTRL_BASE + 0x9cu)
#define GPIO_IN1              (SYSCTRL_BASE + 0xbcu)
#define IO_CON1               (SYSCTRL_BASE + 0xd4u)
#define TIMER2_CTRL           (SYSCTRL_BASE + 0x1cu)
#define TIMER2_LIVE           (SYSCTRL_BASE + 0x104u)

#define UART1_BASE            0x20026000u
#define UART1_CTRL            (UART1_BASE + 0x00u)
#define UART1_STATUS          (UART1_BASE + 0x04u)
#define UART1_CFG             (UART1_BASE + 0x08u)

#define CLOCK_GPIO            14u
#define DATA_GPIO             15u
#define CLOCK_MASK            (1u << CLOCK_GPIO)
#define DATA_MASK             (1u << DATA_GPIO)
#define LINE_MASK             (CLOCK_MASK | DATA_MASK)
/* AK7802 switches GPIO14 and GPIO15 as one group: 0 is GPIO, 1 is UART1. */
#define UART1_MUX_MASK        (1u << 9)

#define RESULT_BASE           0x32008000u
#define RESULT_MAGIC          0x30504454u
#define RESULT_VERSION        1u
#define TIMER_HZ              12000000u
#define TIMER_COUNT_MASK      0x03ffffffu
#define TIMER_ENABLE          (1u << 26)
#define TIMER_LOAD            (1u << 27)
#define CAPTURE_TICKS         (4u * TIMER_HZ)
#define IO_TIMEOUT_TICKS      (TIMER_HZ / 10u)
#define INHIBIT_TICKS         ((136u * TIMER_HZ) / 1000000u)

#define RX_CAPACITY           256u
#define TRACE_CAPACITY        2048u

#define RX_VALID              (1u << 8)
#define RX_START_ERROR        (1u << 9)
#define RX_PARITY_ERROR       (1u << 10)
#define RX_STOP_ERROR         (1u << 11)
#define RX_PHASE_SHIFT        16u
#define RX_PHASE_ENABLE       1u
#define RX_PHASE_DATA         2u
#define RX_PHASE_DISABLE      3u
#define RX_PHASE_ID_ACK       4u
#define RX_PHASE_ID_VALUE     5u
#define RX_PHASE_STATUS_ACK   6u
#define RX_PHASE_STATUS_DATA  7u
#define RX_PHASE_PRE_DISABLE  8u
#define RX_PHASE_REMOTE_ACK   9u
#define RX_PHASE_POLL_ACK     10u
#define RX_PHASE_POLL_DATA    11u
#define RX_PHASE_STREAM_ACK   12u
#define RX_PHASE_POST_DISABLE 13u
#define RX_PHASE_INIT_ACK     14u
#define RX_PHASE_INIT_EXTRA   15u
#define RX_PHASE_INIT_DATA    16u

struct timed_value {
    uint32_t ticks;
    uint32_t value;
};

struct probe_result {
    uint32_t magic;
    uint32_t version;
    uint32_t mode;
    uint32_t complete;
    uint32_t timer_hz;
    uint32_t duration_ticks;
    uint32_t clock_gpio;
    uint32_t data_gpio;
    uint32_t initial_levels;
    uint32_t final_levels;
    uint32_t timer2_before;
    uint32_t sharepin_con2_before;
    uint32_t sharepin_con1_before;
    uint32_t gpio_dir1_before;
    uint32_t gpio_out1_before;
    uint32_t gpio_pull1_before;
    uint32_t gpio_in1_before;
    uint32_t io_con1_before;
    uint32_t uart1_ctrl_before;
    uint32_t uart1_status_before;
    uint32_t uart1_cfg_before;
    uint32_t sharepin_con2_after;
    uint32_t sharepin_con1_after;
    uint32_t gpio_dir1_after;
    uint32_t gpio_out1_after;
    uint32_t gpio_pull1_after;
    uint32_t gpio_in1_after;
    uint32_t io_con1_after;
    uint32_t uart1_ctrl_after;
    uint32_t uart1_status_after;
    uint32_t uart1_cfg_after;
    uint32_t enable_send_status;
    uint32_t enable_reply;
    uint32_t disable_send_status;
    uint32_t disable_reply;
    uint32_t rx_count;
    uint32_t rx_frame_errors;
    uint32_t trace_count;
    uint32_t trace_dropped;
    uint32_t timer_running;
    uint32_t id_send_status;
    uint32_t status_send_status;
    uint32_t poll_send_failures;
    uint32_t poll_reply_errors;
    struct timed_value rx[RX_CAPACITY];
    struct timed_value trace[TRACE_CAPACITY];
};

_Static_assert(sizeof(struct probe_result) == 18608u,
               "result layout must match decode.py");

struct pin_state {
    uint32_t sharepin_con2;
    uint32_t sharepin_con1;
    uint32_t dir1;
    uint32_t out1;
};

static volatile struct probe_result *const result =
    (volatile struct probe_result *)(uintptr_t)RESULT_BASE;
static uint32_t timer_origin;
static uint32_t last_levels;
static uint32_t timer_running;

static uint32_t timer_raw(void)
{
    return REG32(TIMER2_LIVE) & TIMER_COUNT_MASK;
}

static uint32_t elapsed_from(uint32_t start)
{
    return (start - timer_raw()) & TIMER_COUNT_MASK;
}

static uint32_t elapsed_ticks(void)
{
    return elapsed_from(timer_origin);
}

static void timer_start(void)
{
    uint32_t initial;

    REG32(TIMER2_CTRL) = TIMER_COUNT_MASK;
    REG32(TIMER2_CTRL) = TIMER_COUNT_MASK | TIMER_ENABLE | TIMER_LOAD;
    initial = timer_raw();
    timer_running = 0u;
    for (uint32_t i = 0; i < 100000u; i++) {
        if (timer_raw() != initial) {
            timer_running = 1u;
            break;
        }
    }
    timer_origin = timer_raw();
}

static uint32_t raw_to_levels(uint32_t raw)
{
    return ((raw & CLOCK_MASK) ? 1u : 0u) |
           ((raw & DATA_MASK) ? 2u : 0u);
}

static uint32_t sample_lines(void)
{
    uint32_t levels = raw_to_levels(REG32(GPIO_IN1));

    if (levels != last_levels) {
        uint32_t index = result->trace_count;

        if (index < TRACE_CAPACITY) {
            result->trace[index].ticks = elapsed_ticks();
            result->trace[index].value = levels;
            result->trace_count = index + 1u;
        } else {
            result->trace_dropped++;
        }
        last_levels = levels;
    }

    return levels;
}

#if PROBE_ACTIVE
static int wait_lines(uint32_t mask, uint32_t value, uint32_t timeout_ticks)
{
    uint32_t start = timer_raw();

    while ((sample_lines() & mask) != value) {
        if (elapsed_from(start) >= timeout_ticks)
            return 0;
    }

    return 1;
}

static void delay_ticks(uint32_t ticks)
{
    uint32_t start = timer_raw();

    while (elapsed_from(start) < ticks)
        sample_lines();
}
#endif

static void snapshot_before(void)
{
    result->timer2_before = REG32(TIMER2_CTRL);
    result->sharepin_con2_before = REG32(SHAREPIN_CON2);
    result->sharepin_con1_before = REG32(SHAREPIN_CON1);
    result->gpio_dir1_before = REG32(GPIO_DIR1);
    result->gpio_out1_before = REG32(GPIO_OUT1);
    result->gpio_pull1_before = REG32(GPIO_PULL1);
    result->gpio_in1_before = REG32(GPIO_IN1);
    result->io_con1_before = REG32(IO_CON1);
    result->uart1_ctrl_before = REG32(UART1_CTRL);
    result->uart1_status_before = REG32(UART1_STATUS);
    result->uart1_cfg_before = REG32(UART1_CFG);
}

static void snapshot_after(void)
{
    result->sharepin_con2_after = REG32(SHAREPIN_CON2);
    result->sharepin_con1_after = REG32(SHAREPIN_CON1);
    result->gpio_dir1_after = REG32(GPIO_DIR1);
    result->gpio_out1_after = REG32(GPIO_OUT1);
    result->gpio_pull1_after = REG32(GPIO_PULL1);
    result->gpio_in1_after = REG32(GPIO_IN1);
    result->io_con1_after = REG32(IO_CON1);
    result->uart1_ctrl_after = REG32(UART1_CTRL);
    result->uart1_status_after = REG32(UART1_STATUS);
    result->uart1_cfg_after = REG32(UART1_CFG);
}

static void clear_result(void)
{
    volatile uint32_t *word = (volatile uint32_t *)result;
    uint32_t words = (uint32_t)(sizeof(*result) / sizeof(uint32_t));

    for (uint32_t i = 0; i < words; i++)
        word[i] = 0u;

    result->magic = RESULT_MAGIC;
    result->version = RESULT_VERSION;
    result->mode = PROBE_INIT ? 4u : (PROBE_POLL ? 3u :
                   (PROBE_DIAGNOSTIC ? 2u : (PROBE_ACTIVE ? 1u : 0u)));
    result->timer_hz = TIMER_HZ;
    result->duration_ticks = CAPTURE_TICKS;
    result->clock_gpio = CLOCK_GPIO;
    result->data_gpio = DATA_GPIO;
    result->enable_send_status = 0xffffffffu;
    result->enable_reply = 0xffffffffu;
    result->disable_send_status = 0xffffffffu;
    result->disable_reply = 0xffffffffu;
    result->id_send_status = 0xffffffffu;
    result->status_send_status = 0xffffffffu;
}

#if PROBE_ACTIVE
static void line_release(uint32_t mask)
{
    REG32(GPIO_DIR1) |= mask;
    sample_lines();
}

static void line_low(uint32_t mask)
{
    REG32(GPIO_OUT1) &= ~mask;
    REG32(GPIO_DIR1) &= ~mask;
    sample_lines();
}

static void set_data_bit(uint32_t bit)
{
    if (bit)
        line_release(DATA_MASK);
    else
        line_low(DATA_MASK);
}

static struct pin_state configure_gpio_lines(void)
{
    struct pin_state saved;

    saved.sharepin_con2 = REG32(SHAREPIN_CON2);
    saved.sharepin_con1 = REG32(SHAREPIN_CON1);
    saved.dir1 = REG32(GPIO_DIR1);
    saved.out1 = REG32(GPIO_OUT1);

    REG32(GPIO_DIR1) |= LINE_MASK;
    REG32(GPIO_OUT1) &= ~LINE_MASK;
    REG32(SHAREPIN_CON1) &= ~UART1_MUX_MASK;
    sample_lines();

    return saved;
}

static void restore_gpio_lines(const struct pin_state *saved)
{
    REG32(GPIO_DIR1) |= LINE_MASK;
    REG32(GPIO_OUT1) = saved->out1;
    REG32(SHAREPIN_CON2) = saved->sharepin_con2;
    REG32(SHAREPIN_CON1) = saved->sharepin_con1;
    REG32(GPIO_DIR1) = saved->dir1;
    sample_lines();
}

static uint32_t ps2_send(uint8_t byte)
{
    uint32_t parity = 1u;

    line_release(LINE_MASK);
    if (!wait_lines(3u, 3u, IO_TIMEOUT_TICKS))
        return 1u;

    line_low(CLOCK_MASK);
    delay_ticks(INHIBIT_TICKS);
    line_low(DATA_MASK);
    line_release(CLOCK_MASK);

    if (!wait_lines(1u, 1u, IO_TIMEOUT_TICKS))
        return 2u;
    if (!wait_lines(1u, 0u, IO_TIMEOUT_TICKS))
        return 3u;

    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t bit = (byte >> i) & 1u;

        set_data_bit(bit);
        parity ^= bit;
        if (!wait_lines(1u, 1u, IO_TIMEOUT_TICKS))
            return 4u;
        if (!wait_lines(1u, 0u, IO_TIMEOUT_TICKS))
            return 5u;
    }

    set_data_bit(parity);
    if (!wait_lines(1u, 1u, IO_TIMEOUT_TICKS))
        return 6u;
    if (!wait_lines(1u, 0u, IO_TIMEOUT_TICKS))
        return 7u;

    line_release(DATA_MASK);
    if (!wait_lines(1u, 1u, IO_TIMEOUT_TICKS))
        return 8u;
    if (!wait_lines(1u, 0u, IO_TIMEOUT_TICKS))
        return 9u;
    if (!wait_lines(2u, 0u, IO_TIMEOUT_TICKS))
        return 10u;
    if (!wait_lines(1u, 1u, IO_TIMEOUT_TICKS))
        return 11u;
    if (!wait_lines(2u, 2u, IO_TIMEOUT_TICKS))
        return 12u;

    return 0u;
}

static int ps2_read_byte(uint32_t phase, uint32_t timeout_ticks,
                         uint32_t *packed_value)
{
    uint32_t bits = 0u;
    uint32_t flags = 0u;
    uint32_t parity = 0u;

    for (uint32_t i = 0; i < 11u; i++) {
        uint32_t level;

        if (!wait_lines(1u, 0u, timeout_ticks))
            return 0;
        level = (sample_lines() >> 1) & 1u;
        bits |= level << i;
        if (!wait_lines(1u, 1u, timeout_ticks))
            return 0;
    }

    if (bits & 1u)
        flags |= RX_START_ERROR;
    for (uint32_t i = 1u; i <= 9u; i++)
        parity ^= (bits >> i) & 1u;
    if (parity != 1u)
        flags |= RX_PARITY_ERROR;
    if (((bits >> 10) & 1u) == 0u)
        flags |= RX_STOP_ERROR;
    if (flags == 0u)
        flags = RX_VALID;

    *packed_value = ((bits >> 1) & 0xffu) | flags |
                    (phase << RX_PHASE_SHIFT);
    return 1;
}

static uint32_t store_rx(uint32_t phase, uint32_t timeout_ticks)
{
    uint32_t packed;
    uint32_t index;

    if (!ps2_read_byte(phase, timeout_ticks, &packed))
        return 0xffffffffu;

    index = result->rx_count;
    if (index < RX_CAPACITY) {
        result->rx[index].ticks = elapsed_ticks();
        result->rx[index].value = packed;
        result->rx_count = index + 1u;
    }
    if ((packed & RX_VALID) == 0u)
        result->rx_frame_errors++;

    return packed;
}

#if PROBE_DIAGNOSTIC || PROBE_POLL || PROBE_INIT
static int is_reply(uint32_t packed, uint8_t expected)
{
    return (packed & RX_VALID) != 0u && (packed & 0xffu) == expected;
}
#endif

#if PROBE_DIAGNOSTIC
static void run_diagnostic(void)
{
    struct pin_state saved = configure_gpio_lines();
    uint32_t reply;

    result->disable_send_status = ps2_send(0xf5u);
    if (result->disable_send_status == 0u)
        result->disable_reply = store_rx(RX_PHASE_DISABLE, IO_TIMEOUT_TICKS);

    result->id_send_status = ps2_send(0xf2u);
    if (result->id_send_status == 0u) {
        reply = store_rx(RX_PHASE_ID_ACK, IO_TIMEOUT_TICKS);
        if (is_reply(reply, 0xfau))
            store_rx(RX_PHASE_ID_VALUE, IO_TIMEOUT_TICKS);
    }

    result->status_send_status = ps2_send(0xe9u);
    if (result->status_send_status == 0u) {
        reply = store_rx(RX_PHASE_STATUS_ACK, IO_TIMEOUT_TICKS);
        if (is_reply(reply, 0xfau)) {
            for (uint32_t i = 0; i < 3u; i++)
                store_rx(RX_PHASE_STATUS_DATA, IO_TIMEOUT_TICKS);
        }
    }

    restore_gpio_lines(&saved);
}
#elif PROBE_POLL
static void run_poll(void)
{
    struct pin_state saved = configure_gpio_lines();
    uint32_t reply;
    uint32_t capture_start;

    result->id_send_status = ps2_send(0xf5u);
    if (result->id_send_status == 0u)
        store_rx(RX_PHASE_PRE_DISABLE, IO_TIMEOUT_TICKS);

    result->enable_send_status = ps2_send(0xf0u);
    if (result->enable_send_status == 0u)
        result->enable_reply = store_rx(RX_PHASE_REMOTE_ACK, IO_TIMEOUT_TICKS);

    capture_start = timer_raw();
    while (elapsed_from(capture_start) < CAPTURE_TICKS) {
        uint32_t cycle_start = timer_raw();
        uint32_t send_status = ps2_send(0xebu);

        if (send_status != 0u) {
            result->poll_send_failures++;
        } else {
            reply = store_rx(RX_PHASE_POLL_ACK, IO_TIMEOUT_TICKS);
            if (is_reply(reply, 0xfau)) {
                for (uint32_t i = 0; i < 3u; i++)
                    store_rx(RX_PHASE_POLL_DATA, IO_TIMEOUT_TICKS);
            } else {
                result->poll_reply_errors++;
            }
        }

        while (elapsed_from(cycle_start) < TIMER_HZ / 10u &&
               elapsed_from(capture_start) < CAPTURE_TICKS)
            sample_lines();
    }

    result->disable_send_status = ps2_send(0xeau);
    if (result->disable_send_status == 0u)
        result->disable_reply = store_rx(RX_PHASE_STREAM_ACK, IO_TIMEOUT_TICKS);

    result->status_send_status = ps2_send(0xf5u);
    if (result->status_send_status == 0u)
        store_rx(RX_PHASE_POST_DISABLE, IO_TIMEOUT_TICKS);

    restore_gpio_lines(&saved);
}
#elif PROBE_INIT
static int init_command(uint8_t command, uint32_t step, uint32_t extra_count,
                        uint32_t extra_timeout)
{
    uint32_t send_status = ps2_send(command);
    uint32_t reply;

    if (send_status != 0u) {
        result->poll_send_failures = step;
        result->poll_reply_errors = send_status;
        return 0;
    }

    reply = store_rx(RX_PHASE_INIT_ACK, IO_TIMEOUT_TICKS);
    if (!is_reply(reply, 0xfau)) {
        result->poll_send_failures = step;
        result->poll_reply_errors = reply;
        return 0;
    }

    for (uint32_t i = 0; i < extra_count; i++) {
        reply = store_rx(RX_PHASE_INIT_EXTRA, extra_timeout);
        if (reply == 0xffffffffu || (reply & RX_VALID) == 0u) {
            result->poll_send_failures = step;
            result->poll_reply_errors = reply;
            return 0;
        }
    }

    return 1;
}

static void run_init(void)
{
    /* Run the complete PS/2 touchpad initialization sequence. */
    static const uint8_t commands[] = {
        0xf3u, 0xc8u, 0xf3u, 0x64u, 0xf3u, 0x50u, 0xf2u,
        0xf3u, 0x0au, 0xf2u, 0xe8u, 0x03u, 0xe6u, 0xf3u,
        0x14u, 0xf4u,
    };
    struct pin_state saved = configure_gpio_lines();
    uint32_t capture_start;

    result->poll_send_failures = 0xffffffffu;
    if (!init_command(0xffu, 0u, 2u, TIMER_HZ))
        goto disable;

    for (uint32_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        uint32_t extra_count = commands[i] == 0xf2u ? 1u : 0u;

        if (!init_command(commands[i], i + 1u, extra_count,
                          IO_TIMEOUT_TICKS))
            goto disable;
    }

    capture_start = timer_raw();
    while (elapsed_from(capture_start) < CAPTURE_TICKS) {
        uint32_t remaining = CAPTURE_TICKS - elapsed_from(capture_start);
        uint32_t timeout = remaining < IO_TIMEOUT_TICKS ? remaining : IO_TIMEOUT_TICKS;

        if (timeout == 0u)
            break;
        store_rx(RX_PHASE_INIT_DATA, timeout);
    }

disable:
    result->disable_send_status = ps2_send(0xf5u);
    if (result->disable_send_status == 0u)
        result->disable_reply = store_rx(RX_PHASE_POST_DISABLE, IO_TIMEOUT_TICKS);
    restore_gpio_lines(&saved);
}
#else
static void run_active(void)
{
    struct pin_state saved = configure_gpio_lines();
    uint32_t capture_start;

    result->enable_send_status = ps2_send(0xf4u);
    if (result->enable_send_status == 0u)
        result->enable_reply = store_rx(RX_PHASE_ENABLE, IO_TIMEOUT_TICKS);

    capture_start = timer_raw();
    while (elapsed_from(capture_start) < CAPTURE_TICKS) {
        uint32_t remaining = CAPTURE_TICKS - elapsed_from(capture_start);
        uint32_t timeout = remaining < IO_TIMEOUT_TICKS ? remaining : IO_TIMEOUT_TICKS;

        if (timeout == 0u)
            break;
        store_rx(RX_PHASE_DATA, timeout);
    }

    result->disable_send_status = ps2_send(0xf5u);
    if (result->disable_send_status == 0u)
        result->disable_reply = store_rx(RX_PHASE_DISABLE, IO_TIMEOUT_TICKS);

    restore_gpio_lines(&saved);
}
#endif
#endif

#if !PROBE_ACTIVE
static void run_passive(void)
{
    uint32_t start = timer_raw();

    while (elapsed_from(start) < CAPTURE_TICKS)
        sample_lines();
}
#endif

void stub_main(void)
{
    clear_result();
    snapshot_before();
    timer_start();
    result->timer_running = timer_running;

    last_levels = raw_to_levels(REG32(GPIO_IN1));
    result->initial_levels = last_levels;

    if (timer_running) {
#if PROBE_ACTIVE
#if PROBE_DIAGNOSTIC
        run_diagnostic();
#elif PROBE_POLL
        run_poll();
#elif PROBE_INIT
        run_init();
#else
        run_active();
#endif
#else
        run_passive();
#endif
    }

    result->final_levels = sample_lines();
    snapshot_after();
    REG32(TIMER2_CTRL) = 0u;
    result->complete = 1u;
}
