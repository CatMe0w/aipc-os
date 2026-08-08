    .section .text.entry, "ax"
    .arm
    .global _start

/* Save openNBOOT's context into g_regs (.data, survives bss zeroing). */
_start:
    str     r0, [sp, #-4]
    ldr     r0, =g_regs
    add     r0, r0, #4
    stmia   r0, {r1-r12}
    sub     r0, r0, #4
    ldr     r1, [sp, #-4]
    str     r1, [r0, #0]
    str     sp, [r0, #52]
    str     lr, [r0, #56]
    adr     r1, _start
    str     r1, [r0, #60]
    mrs     r1, cpsr
    str     r1, [r0, #64]

    ldr     sp, =_stack_top

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    bl      gdbstub_main
2:  b       2b
