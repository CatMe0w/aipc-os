#include "musb.h"
#include "rsp.h"
#include "trace.h"
#include "lcd_timing.h"

void gdbstub_main(void)
{
    trace_init();
    trace_puts("aipc lcd-timing payload\n");

    lcd_timing_run();

    bp_install();
    musb_init();
    trace_puts("musb up, polling\n");

    for (;;)
        musb_poll();
}
