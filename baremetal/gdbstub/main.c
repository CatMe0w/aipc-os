#include "musb.h"
#include "rsp.h"
#include "trace.h"

#define REPORT_INTERVAL 0x00200000u
#define REPORT_LIMIT    40u

void gdbstub_main(void)
{
    uint32_t ticks = 0;
    uint32_t reports = 0;
    uint32_t last_activity = 0xFFFFFFFFu;
    uint32_t cpsr, sp;

    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    __asm__ volatile ("mov %0, sp" : "=r"(sp));

    trace_init();
    trace_puts("aipc gdbstub stage 2\n");
    trace_reg("cpsr", cpsr);
    trace_reg("sp", sp);

    bp_install();
    musb_init();
    trace_puts("musb up, polling\n");

    for (;;) {
        musb_poll();

        if (++ticks >= REPORT_INTERVAL) {
            uint32_t activity = musb_activity();

            ticks = 0;
            if (activity != last_activity && reports < REPORT_LIMIT) {
                last_activity = activity;
                reports++;
                musb_report();
            }
        }
    }
}
