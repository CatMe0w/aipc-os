#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL_BASE    0x08000000u
#define SYSCTRL(off)    REG32(SYSCTRL_BASE + (off))

#define L2_INT_REG      0x4cu
#define L2_TIMER_ENS    (0x1fu << 1)

#define SUBJECT         0u      /* timer1, the one the clock event driver uses */
#define CTRL_OFF        (0x18u + 4u * SUBJECT)
#define LIVE_OFF        (0x100u + 4u * SUBJECT)

#define COUNT_MASK      0x03ffffffu
#define CTRL_EN         (1u << 26)
#define CTRL_LOAD       (1u << 27)
#define CTRL_CLEAR      (1u << 28)
#define CTRL_STA        (1u << 29)

#define TEST_COUNT      0x00100000u     /* 87 ms at 12 MHz */
#define ATTEMPTS        1000u
#define SAMPLES         8u

#define RESULT_BASE     0x32008000u
#define RESULT_MAGIC    0x334c4b54u
#define RESULT_VERSION  1u

struct load_result {
    uint32_t magic;
    uint32_t version;
    uint32_t complete;
    uint32_t count;

    /* One shot inspection of each form. */
    uint32_t single_ctrl_after;
    uint32_t single_live[SAMPLES];
    uint32_t two_ctrl_after;
    uint32_t two_live[SAMPLES];

    /* How often each form sets the interrupt status at once. */
    uint32_t attempts;
    uint32_t single_immediate_sta;
    uint32_t two_immediate_sta;
};

static struct load_result *const res = (struct load_result *)RESULT_BASE;

/* Exactly what the driver does for a shutdown: one write, count field zero. */
static void stop(void)
{
    SYSCTRL(CTRL_OFF) = CTRL_CLEAR;
}

void stub_main(void)
{
    uint32_t l2_entry;
    uint32_t i;

    res->magic = RESULT_MAGIC;
    res->version = RESULT_VERSION;
    res->complete = 0u;
    res->count = TEST_COUNT;
    res->attempts = ATTEMPTS;

    l2_entry = SYSCTRL(L2_INT_REG);
    SYSCTRL(L2_INT_REG) &= ~L2_TIMER_ENS;

    /* Form A: one write carries the new count and the LOAD strobe. */
    stop();
    SYSCTRL(CTRL_OFF) = TEST_COUNT | CTRL_EN | CTRL_LOAD;
    res->single_ctrl_after = SYSCTRL(CTRL_OFF);
    for (i = 0; i < SAMPLES; i++)
        res->single_live[i] = SYSCTRL(LIVE_OFF);
    stop();

    /* Form B: the count settles before the LOAD strobe. */
    stop();
    SYSCTRL(CTRL_OFF) = TEST_COUNT;
    SYSCTRL(CTRL_OFF) = TEST_COUNT | CTRL_EN | CTRL_LOAD;
    res->two_ctrl_after = SYSCTRL(CTRL_OFF);
    for (i = 0; i < SAMPLES; i++)
        res->two_live[i] = SYSCTRL(LIVE_OFF);
    stop();

    /* Repeat both forms and count how often the status comes up at once. */
    res->single_immediate_sta = 0u;
    for (i = 0; i < ATTEMPTS; i++) {
        stop();
        SYSCTRL(CTRL_OFF) = TEST_COUNT | CTRL_EN | CTRL_LOAD;
        if (SYSCTRL(CTRL_OFF) & CTRL_STA)
            res->single_immediate_sta++;
    }
    stop();

    res->two_immediate_sta = 0u;
    for (i = 0; i < ATTEMPTS; i++) {
        stop();
        SYSCTRL(CTRL_OFF) = TEST_COUNT;
        SYSCTRL(CTRL_OFF) = TEST_COUNT | CTRL_EN | CTRL_LOAD;
        if (SYSCTRL(CTRL_OFF) & CTRL_STA)
            res->two_immediate_sta++;
    }
    stop();

    SYSCTRL(CTRL_OFF) = 0u;
    SYSCTRL(L2_INT_REG) = (SYSCTRL(L2_INT_REG) & ~L2_TIMER_ENS) |
                          (l2_entry & L2_TIMER_ENS);

    res->complete = 1u;
}
