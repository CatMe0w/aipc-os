/*
 * Calls a function with the stack moved into L2 SRAM, so that the callee
 * survives an ASIC clock change that makes DDR unusable. The caller frame
 * stays on the DDR stack.
 *
 * r0 = function, r1 = argument, r2 = L2 stack top.
 */

    .section .text, "ax"
    .arm
    .global l2_call
    .type l2_call, %function

l2_call:
    push    {r4, r5, lr}
    mov     r4, sp
    mov     r5, r0
    mov     r0, r1
    mov     sp, r2
    blx     r5
    mov     sp, r4
    pop     {r4, r5, pc}

    .size l2_call, . - l2_call

/*
 * Two argument form. r0 = function, r1 = arg0, r2 = arg1, r3 = L2 stack top.
 */
    .global l2_call2
    .type l2_call2, %function

l2_call2:
    push    {r4, r5, lr}
    mov     r4, sp
    mov     r5, r0
    mov     r0, r1
    mov     r1, r2
    mov     sp, r3
    blx     r5
    mov     sp, r4
    pop     {r4, r5, pc}

    .size l2_call2, . - l2_call2
