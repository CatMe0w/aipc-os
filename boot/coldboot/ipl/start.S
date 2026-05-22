    .section .text.entry, "ax"
    .arm
    .global _start

/* IPL is linked to run at 0x32000000 but may be loaded at any address (nboot
 * places us at 0x30038000; usbboot uploads to wherever --addr says). The
 * entry stub copies our entire payload to the link address before jumping
 * into C code, freeing the 0x30037FD4..0x3009BFFF region for the BAK load
 * during fallback. */

_start:
    adr     r0, _start          /* r0 = current load address of _start */
    ldr     r1, =_start         /* r1 = link-time _start address (literal) */
    cmp     r0, r1
    beq     _at_link

    ldr     r2, =_payload_end
    sub     r2, r2, r1          /* r2 = payload size in bytes */
_copy_loop:
    ldr     r12, [r0], #4
    str     r12, [r1], #4
    subs    r2, r2, #4
    bne     _copy_loop

    ldr     pc, =_at_link       /* jump to link-time address */

_at_link:
    ldr     sp, =_stack_top

    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
_bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     _bss_loop

    bl      ipl_main

_hang:
    b       _hang
