/* Shared macros for AIPC DDR init stubs. */

    .macro mmio_write addr, value
    ldr     r0, =\value
    ldr     r1, =\addr
    str     r0, [r1]
    .endm

    .macro ddr_delay ticks
    ldr     r0, =\ticks
    ldr     r12, =0x00000FA8
    blx     r12
    .endm

    .macro ddr_sequence refresh_value, uart_value=0
    mmio_write 0x080000DC, 0x00000000
    mmio_write 0x08000004, 0x0000D000
    ddr_delay  200
    .if \uart_value
    mmio_write 0x20026000, \uart_value
    .endif
    mmio_write 0x08000064, 0x08000000
    mmio_write 0x080000A8, 0x04000000
    mmio_write 0x2002D004, 0x0F506B95
    ddr_delay  968

    ldr     r1, =0x2002D000

    ldr     r0, =0x40170000
    str     r0, [r1]
    ldr     r0, =0x40120400
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x40104000
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x40100123
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x40120400
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x40110000
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x40110000
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x40100023
    str     r0, [r1]
    ddr_delay  1
    ldr     r0, =0x60170000
    str     r0, [r1]

    ldr     r0, =\refresh_value
    str     r0, [r1, #8]
    .endm

    .macro stub_entry
    .section .text, "ax"
    .arm
    .global _start

_start:
    ldr     sp, =0x4800157C
    stmdb   sp!, {lr}
    .endm

    .macro stub_return
    ldmia   sp!, {lr}
    bx      lr
    .endm
