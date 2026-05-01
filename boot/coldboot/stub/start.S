    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    mov r0, #0
    mov r1, #0
    mov r2, #0
    ldr pc, =0x30008000
