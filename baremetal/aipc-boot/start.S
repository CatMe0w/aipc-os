    .section .text.entry, "ax"
    .arm
    .global _start

/* openNBOOT enters here in SVC mode. IRQ and FIQ are masked in CPSR, and the
 * SD pads are already released. Nothing else is guaranteed. */

#define MODE_SVC_MASKED 0xD3

_start:
    msr     cpsr_fc, #MODE_SVC_MASKED
    ldr     sp, =_stack_top

    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
_bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     _bss_loop

    bl      boot_main

_hang:
    b       _hang
