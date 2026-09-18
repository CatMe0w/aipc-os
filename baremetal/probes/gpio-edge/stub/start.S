    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    ldr     sp, =0x32020000
    mrs     r4, cpsr
    orr     r0, r4, #0xc0
    msr     cpsr_c, r0
    bl      stub_main
    msr     cpsr_c, r4
    .word   0xE7FFDEFE
