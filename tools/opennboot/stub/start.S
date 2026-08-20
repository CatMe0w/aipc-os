    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    mov     r4, lr
    ldr     sp, =STUB_STACK
    bl      stub_main
    mov     pc, r4
