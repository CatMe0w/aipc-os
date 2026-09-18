#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define SYSCTRL_BASE          0x08000000u
#define CLOCK_DIV1            (SYSCTRL_BASE + 0x04u)
#define TIMER2_CTRL           (SYSCTRL_BASE + 0x1cu)
#define IRQ_MASK              (SYSCTRL_BASE + 0x34u)
#define WGPIO_POLARITY        (SYSCTRL_BASE + 0x3cu)
#define WGPIO_CLEAR           (SYSCTRL_BASE + 0x40u)
#define WGPIO_ENABLE          (SYSCTRL_BASE + 0x44u)
#define WGPIO_STATUS          (SYSCTRL_BASE + 0x48u)
#define SYS_INT_ENABLE        (SYSCTRL_BASE + 0x4cu)
#define SHAREPIN_CON1         (SYSCTRL_BASE + 0x78u)
#define GPIO_DIR1             (SYSCTRL_BASE + 0x7cu)
#define TIMER2_READ           (SYSCTRL_BASE + 0x104u)
#define GPIO_IN1              (SYSCTRL_BASE + 0xbcu)
#define INT_STATUS            (SYSCTRL_BASE + 0xccu)
#define GPIO_INT_EN1          (SYSCTRL_BASE + 0xe0u)
#define GPIO_INTP1            (SYSCTRL_BASE + 0xf0u)

#define POWER_KEY_MASK        (1u << 3)
#define CONTROL_GPIO_MASK     (1u << 13)
#define CONTROL_WGPIO_MASK    (1u << 6)
#define RTC_WAKEUP_ENABLE     (1u << 16)
#define SYSCTRL_PARENT_ENABLE (1u << 27)
#define GPIO_PARENT_ENABLE    (1u << 10)
#define WGPIO_PARENT_ENABLE   (1u << 7)
#define SYSCTRL_PARENT_PENDING (1u << 27)
#define WGPIO_PARENT_PENDING  (1u << 23)

#define TIMER_MASK            0x03ffffffu
#define TIMER_ENABLE          (1u << 26)
#define TIMER_HZ              12000000u
#define CONTROL_TIMEOUT_TICKS (60u * TIMER_HZ)
#define EDGE_TIMEOUT_TICKS    (30u * TIMER_HZ)
#define SETTLE_TICKS          (TIMER_HZ / 1000u)

#define RESULT_ADDR           0x32008000u
#define RESULT_MAGIC          0x45475041u
#define RESULT_VERSION        2u

enum probe_status {
    PROBE_WAIT_CARD_PRESENT_TIMEOUT = 1,
    PROBE_WAIT_CARD_REMOVE_TIMEOUT = 2,
    PROBE_WAIT_CARD_INSERT_TIMEOUT = 3,
    PROBE_WAIT_RELEASE_TIMEOUT = 4,
    PROBE_WAIT_PRESS_TIMEOUT = 5,
    PROBE_WAIT_FINAL_RELEASE_TIMEOUT = 6,
    PROBE_COMPLETE = 7,
};

struct snapshot {
    uint32_t gpio_in1;
    uint32_t int_status;
    uint32_t gpio_int_en1;
    uint32_t gpio_intp1;
    uint32_t wgpio_polarity;
    uint32_t wgpio_enable;
    uint32_t wgpio_status;
    uint32_t clock_div1;
};

struct saved_state {
    uint32_t clock_div1;
    uint32_t timer2_ctrl;
    uint32_t wgpio_polarity;
    uint32_t wgpio_enable;
    uint32_t wgpio_status;
    uint32_t irq_mask;
    uint32_t sys_int_enable;
    uint32_t sharepin_con1;
    uint32_t gpio_dir1;
    uint32_t gpio_int_en1;
    uint32_t gpio_intp1;
};

struct probe_result {
    uint32_t magic;
    uint32_t version;
    uint32_t status;
    uint32_t power_key_mask;
    uint32_t sysctrl_parent_mask;
    uint32_t wgpio_parent_mask;
    uint32_t control_gpio_mask;
    uint32_t control_wgpio_mask;
    uint32_t control_falling_input;
    uint32_t control_falling_status;
    uint32_t control_rising_input;
    uint32_t control_rising_status;
    uint32_t falling_new_status;
    uint32_t rising_new_status;
    uint32_t control_falling_int_status;
    uint32_t control_rising_int_status;
    struct saved_state saved;
    struct snapshot initial;
    struct snapshot falling_armed;
    struct snapshot falling_immediate;
    struct snapshot falling_settled;
    struct snapshot rising_armed;
    struct snapshot rising_immediate;
    struct snapshot rising_settled;
    struct snapshot restored;
};

_Static_assert(sizeof(struct probe_result) == 364u,
               "update the host decoder when the result layout changes");

static void snapshot_take(volatile struct snapshot *s)
{
    s->gpio_in1 = REG32(GPIO_IN1);
    s->int_status = REG32(INT_STATUS);
    s->gpio_int_en1 = REG32(GPIO_INT_EN1);
    s->gpio_intp1 = REG32(GPIO_INTP1);
    s->wgpio_polarity = REG32(WGPIO_POLARITY);
    s->wgpio_enable = REG32(WGPIO_ENABLE);
    s->wgpio_status = REG32(WGPIO_STATUS);
    s->clock_div1 = REG32(CLOCK_DIV1);
}

static uint32_t timer_read(void)
{
    return REG32(TIMER2_READ) & TIMER_MASK;
}

static uint32_t timer_delta(uint32_t before, uint32_t after)
{
    return (before - after) & TIMER_MASK;
}

static void delay_ticks(uint32_t ticks)
{
    uint32_t elapsed = 0;
    uint32_t before = timer_read();

    while (elapsed < ticks) {
        uint32_t after = timer_read();

        elapsed += timer_delta(before, after);
        before = after;
    }
}

static int wait_gpio_level(uint32_t mask, uint32_t high,
                           uint32_t timeout_ticks)
{
    uint32_t elapsed = 0;
    uint32_t before = timer_read();

    while (elapsed < timeout_ticks) {
        uint32_t input = REG32(GPIO_IN1) & mask;
        uint32_t after;

        if (!!input == !!high)
            return 1;

        after = timer_read();
        elapsed += timer_delta(before, after);
        before = after;
    }

    return 0;
}

static void wgpio_clear_all(void)
{
    REG32(WGPIO_CLEAR) = 0xffffffffu;
    REG32(WGPIO_CLEAR) = 0u;
}

static uint32_t run_wgpio_control(volatile struct probe_result *result)
{
    REG32(GPIO_DIR1) |= CONTROL_GPIO_MASK;
    if (!wait_gpio_level(CONTROL_GPIO_MASK, 0u, CONTROL_TIMEOUT_TICKS))
        return PROBE_WAIT_CARD_PRESENT_TIMEOUT;

    REG32(WGPIO_ENABLE) = 0u;
    REG32(WGPIO_POLARITY) &= ~CONTROL_WGPIO_MASK;
    wgpio_clear_all();
    REG32(WGPIO_ENABLE) = CONTROL_WGPIO_MASK;
    if (!wait_gpio_level(CONTROL_GPIO_MASK, 1u, CONTROL_TIMEOUT_TICKS))
        return PROBE_WAIT_CARD_REMOVE_TIMEOUT;
    delay_ticks(SETTLE_TICKS);
    result->control_rising_input = REG32(GPIO_IN1) & CONTROL_GPIO_MASK;
    result->control_rising_status = REG32(WGPIO_STATUS);
    result->control_rising_int_status = REG32(INT_STATUS);

    REG32(WGPIO_ENABLE) = 0u;
    REG32(WGPIO_POLARITY) |= CONTROL_WGPIO_MASK;
    wgpio_clear_all();
    REG32(WGPIO_ENABLE) = CONTROL_WGPIO_MASK;
    if (!wait_gpio_level(CONTROL_GPIO_MASK, 0u, CONTROL_TIMEOUT_TICKS))
        return PROBE_WAIT_CARD_INSERT_TIMEOUT;
    delay_ticks(SETTLE_TICKS);
    result->control_falling_input = REG32(GPIO_IN1) & CONTROL_GPIO_MASK;
    result->control_falling_status = REG32(WGPIO_STATUS);
    result->control_falling_int_status = REG32(INT_STATUS);

    REG32(WGPIO_ENABLE) = 0u;
    wgpio_clear_all();
    return 0u;
}

static void restore_state(const struct saved_state *saved)
{
    REG32(GPIO_INT_EN1) &= ~POWER_KEY_MASK;
    REG32(WGPIO_ENABLE) = 0u;
    wgpio_clear_all();

    REG32(GPIO_INTP1) = saved->gpio_intp1;
    REG32(GPIO_INT_EN1) = saved->gpio_int_en1;
    REG32(GPIO_DIR1) = saved->gpio_dir1;
    REG32(SHAREPIN_CON1) = saved->sharepin_con1;
    REG32(WGPIO_POLARITY) = saved->wgpio_polarity;
    REG32(WGPIO_ENABLE) = saved->wgpio_enable;
    REG32(SYS_INT_ENABLE) = saved->sys_int_enable;
    REG32(IRQ_MASK) = saved->irq_mask;
    REG32(CLOCK_DIV1) = saved->clock_div1;
    REG32(TIMER2_CTRL) = saved->timer2_ctrl;
}

void stub_main(void)
{
    volatile struct probe_result *result =
        (volatile struct probe_result *)(uintptr_t)RESULT_ADDR;
    struct saved_state saved;
    uint32_t *words = (uint32_t *)(uintptr_t)RESULT_ADDR;
    uint32_t falling_before;
    uint32_t rising_before;
    uint32_t status;
    uint32_t i;

    for (i = 0; i < sizeof(*result) / sizeof(*words); i++)
        words[i] = 0u;

    saved.clock_div1 = REG32(CLOCK_DIV1);
    saved.timer2_ctrl = REG32(TIMER2_CTRL);
    saved.wgpio_polarity = REG32(WGPIO_POLARITY);
    saved.wgpio_enable = REG32(WGPIO_ENABLE);
    saved.wgpio_status = REG32(WGPIO_STATUS);
    saved.irq_mask = REG32(IRQ_MASK);
    saved.sys_int_enable = REG32(SYS_INT_ENABLE);
    saved.sharepin_con1 = REG32(SHAREPIN_CON1);
    saved.gpio_dir1 = REG32(GPIO_DIR1);
    saved.gpio_int_en1 = REG32(GPIO_INT_EN1);
    saved.gpio_intp1 = REG32(GPIO_INTP1);

    result->magic = RESULT_MAGIC;
    result->version = RESULT_VERSION;
    result->power_key_mask = POWER_KEY_MASK;
    result->sysctrl_parent_mask = SYSCTRL_PARENT_PENDING;
    result->wgpio_parent_mask = WGPIO_PARENT_PENDING;
    result->control_gpio_mask = CONTROL_GPIO_MASK;
    result->control_wgpio_mask = CONTROL_WGPIO_MASK;
    result->saved = saved;

    REG32(TIMER2_CTRL) = TIMER_MASK;
    REG32(TIMER2_CTRL) = TIMER_MASK | TIMER_ENABLE;
    REG32(SHAREPIN_CON1) &= ~1u;
    REG32(GPIO_DIR1) |= POWER_KEY_MASK;
    REG32(CLOCK_DIV1) |= RTC_WAKEUP_ENABLE;
    REG32(IRQ_MASK) |= SYSCTRL_PARENT_ENABLE;
    REG32(SYS_INT_ENABLE) |= GPIO_PARENT_ENABLE | WGPIO_PARENT_ENABLE;
    REG32(GPIO_INT_EN1) &= ~POWER_KEY_MASK;
    REG32(WGPIO_ENABLE) = 0u;
    wgpio_clear_all();

    snapshot_take(&result->initial);

    status = run_wgpio_control(result);
    if (status != 0u) {
        result->status = status;
        goto out;
    }

    if (!wait_gpio_level(POWER_KEY_MASK, 1u, EDGE_TIMEOUT_TICKS)) {
        result->status = PROBE_WAIT_RELEASE_TIMEOUT;
        goto out;
    }

    REG32(GPIO_INTP1) |= POWER_KEY_MASK;
    REG32(GPIO_INT_EN1) |= POWER_KEY_MASK;
    REG32(WGPIO_POLARITY) = 0xffffffffu;
    wgpio_clear_all();
    falling_before = REG32(WGPIO_STATUS);
    REG32(WGPIO_ENABLE) = 0xffffffffu;
    snapshot_take(&result->falling_armed);

    if (!wait_gpio_level(POWER_KEY_MASK, 0u, EDGE_TIMEOUT_TICKS)) {
        result->status = PROBE_WAIT_PRESS_TIMEOUT;
        goto out;
    }

    snapshot_take(&result->falling_immediate);
    delay_ticks(SETTLE_TICKS);
    snapshot_take(&result->falling_settled);
    result->falling_new_status =
        result->falling_settled.wgpio_status & ~falling_before;

    REG32(WGPIO_ENABLE) = 0u;
    REG32(WGPIO_POLARITY) = 0u;
    wgpio_clear_all();
    rising_before = REG32(WGPIO_STATUS);
    REG32(WGPIO_ENABLE) = 0xffffffffu;
    snapshot_take(&result->rising_armed);

    if (!wait_gpio_level(POWER_KEY_MASK, 1u, EDGE_TIMEOUT_TICKS)) {
        result->status = PROBE_WAIT_FINAL_RELEASE_TIMEOUT;
        goto out;
    }

    snapshot_take(&result->rising_immediate);
    delay_ticks(SETTLE_TICKS);
    snapshot_take(&result->rising_settled);
    result->rising_new_status =
        result->rising_settled.wgpio_status & ~rising_before;
    result->status = PROBE_COMPLETE;

out:
    restore_state(&saved);
    snapshot_take(&result->restored);
}
