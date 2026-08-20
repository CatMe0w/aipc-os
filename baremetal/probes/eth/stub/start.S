    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    mov     r4, lr
    ldr     sp, =0x4800157C
    bl      stub_main
    mov     pc, r4
