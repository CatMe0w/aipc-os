    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    ldr     r0, =caller_regs
    stmia   r0, {r4-r11, sp, lr}
    ldr     sp, =_stack_top

    /* Clear BSS. Word stores, because the linker aligns it to 4 bytes. */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
.bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     .bss_loop

    b       doom_main

/* r4 to r11, sp and lr of the caller. It must stay in .data, because
 * _start writes it before the .bss clear. */
    .section .data
    .align  2
    .global caller_regs
caller_regs:
    .space  10 * 4

/* Returns to the caller of _start. The caches and the MMU must be off. */
    .section .text
    .arm
    .global return_to_caller
return_to_caller:
    msr     cpsr_c, #0xD3
    ldr     r0, =caller_regs
    ldmia   r0, {r4-r11, sp, lr}
    mov     r0, #0
    bx      lr
