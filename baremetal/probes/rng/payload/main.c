/*
 * Replaces gdbstub_main(). The measurement runs first, before MUSB starts,
 * and the normal GDB stub body follows it. See the README.
 */

/* musb.h first: it defines REG32 without a guard. */
#include "musb.h"
#include "rsp.h"
#include "trace.h"
#include "probe_api.h"

void gdbstub_main(void)
{
    uint32_t n = probe_init();

    trace_init();
    trace_puts("aipc rng payload\n");
    trace_reg("samples", n);

    bp_install();
    musb_init();
    trace_puts("musb up, polling\n");

    for (;;)
        musb_poll();
}
