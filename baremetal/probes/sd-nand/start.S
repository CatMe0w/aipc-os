    .section .text.entry, "ax"
    .arm
    .global _start
_start:
    mov     r4, lr
    mov     r5, sp
    ldr     sp, =_stack_top
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bl      main
    mov     sp, r5
    mov     pc, r4
