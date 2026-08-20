    .section .text, "ax"
    .arm
    .global rsp_resume

/* void rsp_resume(struct arm_regs *regs)
 * pc goes to [sp, #-4] first, so that it survives the ldmia of r0-r12. */
rsp_resume:
    ldr     r1, [r0, #64]  @ cpsr. A mode change here swaps the banked sp and lr
    msr     cpsr_cxsf, r1

    ldr     r1, [r0, #52]  @ sp
    mov     sp, r1
    ldr     lr, [r0, #56]  @ lr

    ldr     r1, [r0, #60]  @ pc
    str     r1, [sp, #-4]

    ldmia   r0, {r0-r12}
    ldr     pc, [sp, #-4]
