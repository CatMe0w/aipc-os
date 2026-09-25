/*
 * irq_hook replaces OEMInterruptHandler, so it runs in SVC mode with IRQs
 * masked, and r0 holds the interrupted PC. It must return the SYSINTR of the
 * original handler unchanged.
 *
 * A branch replaces the `push {r4-r11, lr}` at the entry of OEMIoControl.
 * ioctl_hook must therefore do that push itself before it jumps to
 * ioctl_resume.
 */

#include "resident.h"

    .section .text.entry, "ax"
    .arm

    .word   RESIDENT_MAGIC
    .word   irq_hook
    .word   irq_orig
    .word   ioctl_hook
    .word   ioctl_resume

irq_hook:
    push    {r4, lr}
    mov     r4, r0
    bl      resident_irq
    mov     r0, r4
    ldr     ip, irq_orig
    blx     ip
    bl      resident_sysintr
    pop     {r4, pc}

irq_orig:
    .word   0

ioctl_hook:
    push    {r0-r3, ip, lr}
    bl      resident_ioctl
    pop     {r0-r3, ip, lr}
    push    {r4-r11, lr}
    ldr     pc, ioctl_resume

ioctl_resume:
    .word   0
