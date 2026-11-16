    .section .text.hooks, "ax"
    .arm
    .global diag_irq_hook
    .global diag_irq_orig
    .global diag_ioctl_hook
    .global diag_ioctl_resume

/* In place of OEMInterruptHandler: SVC mode, IRQs masked, r0 = interrupted
 * PC. It must return the SYSINTR of the original handler unchanged. */
diag_irq_hook:
    push    {r4, lr}
    mov     r4, r0
    bl      resident_irq
    mov     r0, r4
    ldr     ip, diag_irq_orig
    blx     ip
    bl      resident_sysintr
    pop     {r4, pc}

diag_irq_orig:
    .word   0

/* A branch here replaces the `push {r4-r11, lr}` at the entry of
 * OEMIoControl, so diag_ioctl_hook does that push before it resumes. */
diag_ioctl_hook:
    push    {r0-r3, ip, lr}
    bl      resident_ioctl
    pop     {r0-r3, ip, lr}
    push    {r4-r11, lr}
    ldr     pc, diag_ioctl_resume

diag_ioctl_resume:
    .word   0
