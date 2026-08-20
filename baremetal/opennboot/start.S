    .section .text.entry, "ax"
    .arm
    .global _start

/* These SYSCTRL clears must happen before sd_init(). sd.c captures 0x0C at
 * entry and restores it later, and the bootrom clock gating is still live
 * here. */

#define SYSCTRL         0x08000000
#define SYSCTRL_CLKRST  0x0C
#define SYSCTRL_IRQ_MSK 0x34
#define SYSCTRL_FIQ_MSK 0x38

#define MODE_IRQ_MASKED 0xD2
#define MODE_SVC_MASKED 0xD3

_start:
    ldr     r0, =SYSCTRL
    mov     r1, #0
    str     r1, [r0, #SYSCTRL_CLKRST]
    str     r1, [r0, #SYSCTRL_IRQ_MSK]
    str     r1, [r0, #SYSCTRL_FIQ_MSK]

    msr     cpsr_fc, #MODE_IRQ_MASKED
    ldr     sp, =0x30FFFF00
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
