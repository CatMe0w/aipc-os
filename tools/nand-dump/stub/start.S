/*
 * AK7802 NAND dump stub - entry point
 *
 * Loaded to 0x48000200 (L2BUF_01) via USB boot mode.
 * Sets up the stack at the top of L2 buffer SRAM and calls main().
 */

    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    ldr     sp, =0x480017F0
    bl      main
    b       .
