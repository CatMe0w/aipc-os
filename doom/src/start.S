/*
 * AIPC DOOM entry point
 *
 * The bootrom EXECUTE handler calls this as a function (LR = return address).
 * DOOM never returns, so we don't bother preserving LR.
 *
 * Stack is placed at the top of the DDR region allocated for this payload.
 * See linker.ld for the full memory layout.
 */

    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    ldr     sp, =_stack_top

    /* Clear BSS (word loop; linker ensures 4-byte alignment) */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
.bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     .bss_loop

    b       doom_main
