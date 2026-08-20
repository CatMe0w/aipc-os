/*
 * Entry into a loaded payload.
 */

    .section .text, "ax"
    .arm
    .align 5
    .global jump_payload

/* r0 = entry. The ARM boot protocol wants r1 = machine type and r2 = ATAG or
 * DTB pointer. Our kernel has an appended DTB and reads neither register.
 * gdbstub only records the registers that it starts with. */
jump_payload:
    mov     r4, r0
    mov     r0, #0
    mov     r1, #0
    mov     r2, #0
    mov     r3, #0
    mov     pc, r4
