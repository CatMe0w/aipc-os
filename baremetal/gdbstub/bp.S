/*
 * Exception entry. Each vector has its own entry, because the lr bias is
 * different for each exception type. sp is banked and unused here, so it
 * holds &g_regs.
 */

#include "sig.h"

    .section .text
    .arm

    .macro  ENTRY name, signal, bias
    .global \name
\name:
    ldr     sp, =g_regs
    stmia   sp, {r0-r12}
    mov     r0, #\signal
    mov     r1, #\bias
    b       trap_common
    .endm

    ENTRY   bp_undef,  SIG_ILL,  4
    ENTRY   bp_pabort, SIG_SEGV, 4
    ENTRY   bp_dabort, SIG_BUS,  8

/* r0 = signal, r1 = lr bias, sp = g_regs, r0-r12 already saved. */
trap_common:
    sub     r2, lr, r1                  /* the instruction that faulted */
    mrs     r3, spsr                    /* its CPSR */
    str     r2, [sp, #60]
    str     r3, [sp, #64]

    /* Enter the interrupted mode to read its banked sp and lr. */
    mrs     r4, cpsr
    and     r5, r3, #0x1F
    orr     r5, r5, #0xC0
    msr     cpsr_c, r5
    mov     r6, sp
    mov     r7, lr
    msr     cpsr_c, r4
    str     r6, [sp, #52]
    str     r7, [sp, #56]

    /* Reset the stack, so that a trap inside the stub cannot stack frames. */
    msr     cpsr_c, #0xD3               /* SVC, IRQ and FIQ masked */
    ldr     sp, =_stack_top
    b       rsp_trapped                 /* r0 is still the signal */
    .ltorg
