    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    ldr     sp, =0x32020000
    bl      stub_main
    .word   0xE7FFDEFE
