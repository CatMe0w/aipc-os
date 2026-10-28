    .section .text.entry, "ax"
    .arm
    .global _start
    .global probe_done
_start:
    msr     cpsr_c, #0xD3
    ldr     sp, =_stack_top
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bl      main
probe_done:
    .word   0xE7FFDEFE
    b       probe_done
