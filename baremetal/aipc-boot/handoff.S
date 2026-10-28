    .section .text, "ax"
    .arm
    .align 5
    .global jump_payload
    .global call_payload
    .global jump_linux

/* r0 = entry. r0 to r3 start at 0. A kernel with an appended DTB reads
 * neither r1 nor r2. */
jump_payload:
    mov     r4, r0
    mov     r0, #0
    mov     r1, #0
    mov     r2, #0
    mov     r3, #0
    mov     pc, r4

/* r0 = entry. Calls the payload as an AAPCS function, with no arguments. */
call_payload:
    push    {r4, lr}
    mov     r4, r0
    mov     r0, #0
    mov     r1, #0
    mov     r2, #0
    mov     r3, #0
    blx     r4
    pop     {r4, pc}

/* r0 = entry, r1 = ATAG list. With CONFIG_ARM_ATAG_DTB_COMPAT, the kernel
 * decompressor merges the ATAG command line into the appended DTB. */
jump_linux:
    mov     r4, r0
    mov     r2, r1
    mov     r0, #0
    mov     r1, #0
    mov     r3, #0
    mov     pc, r4
