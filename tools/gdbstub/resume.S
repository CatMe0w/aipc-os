    .section .text, "ax"
    .arm
    .global rsp_resume

/* void rsp_resume(struct arm_regs *regs)
 * pc is stashed at [sp, #-4] so it survives the ldmia that clobbers r0-r12. */
rsp_resume:
    ldr     r1, [r0, #64]  @ cpsr, may switch mode: banked sp/lr change
    msr     cpsr_cxsf, r1

    ldr     r1, [r0, #52]  @ sp
    mov     sp, r1
    ldr     lr, [r0, #56]  @ lr

    ldr     r1, [r0, #60]  @ pc
    str     r1, [sp, #-4]

    ldmia   r0, {r0-r12}
    ldr     pc, [sp, #-4]
