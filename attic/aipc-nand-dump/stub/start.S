/*
 * AIPC NAND dump stubs - shared entry point
 *
 * The bootrom's EXECUTE path calls the target as a function.  The stub
 * always returns to the bootrom so the host retains full USB control.
 */

    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    mov     r4, lr
    ldr     sp, =0x4800157C
    bl      stub_main
    mov     pc, r4
