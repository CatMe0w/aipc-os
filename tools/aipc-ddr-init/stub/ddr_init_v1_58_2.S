/*
 * AIPC DDR init stub for the nboot v1.58.2 sequence.
 *
 * Loaded to 0x48000240 and executed via USB boot EXECUTE. The stub performs
 * the same MMIO writes and delays as the type-6 nboot header script, then
 * returns to the bootrom so USB boot mode stays available.
 */

    .include "ddr_init_common.inc"

    stub_entry
    ddr_sequence 0x00057C58
    stub_return
