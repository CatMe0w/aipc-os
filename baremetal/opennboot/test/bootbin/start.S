    .section .text.entry, "ax"
    .arm
    .global _start

/* Writes the handoff state and the image checksum to 0x301E0000, then halts.
 * See README.md for the layout of that result block. */

#define RESULT_BASE 0x301E0000
#define MAGIC       0x42424E4F      /* "ONBB" */
#define DONE        0x600D600D

_start:
    b       _main                   /* file offset 0x00 */
_img_len:
    .word   0                       /* file offset 0x04, patched by pack.py */

_main:
    ldr     r0, =RESULT_BASE
    ldr     r1, =MAGIC
    str     r1, [r0, #0]

    mrs     r2, cpsr
    str     r2, [r0, #4]
    mov     r3, sp
    str     r3, [r0, #8]

    adr     r4, _start              /* the address we really run at */
    str     r4, [r0, #12]

    ldr     r5, [r4, #4]            /* stamped image length */
    str     r5, [r0, #16]

    bic     r6, r5, #3
    add     r6, r4, r6              /* end, rounded down to a whole word */
    mov     r7, r4
    mov     r8, #0
_sum_loop:
    ldr     r9, [r7], #4
    add     r8, r8, r9
    cmp     r7, r6
    blo     _sum_loop
    str     r8, [r0, #20]

    ldr     r1, =DONE
    str     r1, [r0, #24]

_hang:
    b       _hang
