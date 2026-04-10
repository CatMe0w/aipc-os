/*
 * AIPC DDR init stub for the nboot v1.88 sequence.
 *
 * Differences from v1.58.2:
 *   - extra UART write at 0x20026000 before DDR controller programming
 *   - final DDR refresh timing is 0x00037C58
 */

    .include "ddr_init_common.inc"

    stub_entry
    ddr_sequence 0x00037C58, 0x30200433
    stub_return
