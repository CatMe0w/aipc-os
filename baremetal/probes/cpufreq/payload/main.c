/*
 * Replaces gdbstub_main(). The experiment runs first, on a bus that openNBOOT
 * leaves idle, then the normal GDB stub body starts so the result can be read.
 *
 * Boot as BOOT.BIN directly from openNBOOT, not through aipc-boot. aipc-boot
 * starts the LCD, and the LCD DMA master runs on the ASIC clock.
 */

/* musb.h first: it defines REG32 without a guard. */
#include "musb.h"
#include "rsp.h"
#include "trace.h"
#include "probe_api.h"

void gdbstub_main(void)
{
    uint32_t copied = probe_init();

    trace_init();
    trace_puts("aipc cpufreq payload\n");
    trace_reg("l2 worker bytes", copied);

    bp_install();
    musb_init();
    trace_puts("musb up, polling\n");

    for (;;)
        musb_poll();
}
