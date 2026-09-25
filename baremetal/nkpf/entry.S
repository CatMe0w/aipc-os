/*
 * The patched EBOOT handoff jumps here with the MMU and caches off, and r0
 * holds the NK entry. sp is still an EBOOT virtual address, so nkpf cannot
 * use it.
 *
 * EBOOT maps DDR write-back and never cleans the D-cache, so part of NK is
 * still only in dirty lines. Clean the D-cache before anything reads NK or
 * writes DDR. The clean works with the MMU off, because each line is written
 * back to the physical address in its tag.
 */

#include "layout.h"

    .section .text.entry, "ax"
    .arm
    .global _start

_start:
1:  mrc     p15, 0, r15, c7, c14, 3
    bne     1b
    mov     ip, #0
    mcr     p15, 0, ip, c7, c10, 4
    ldr     ip, =saved
    stmia   ip, {r0-r11, sp, lr}
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
    mov     r3, #0
1:  cmp     r1, r2
    strlo   r3, [r1], #4
    blo     1b
    ldr     sp, =NKPF_STACK_TOP
    bl      nkpf_main
    ldr     ip, =saved
    ldmia   ip, {r0-r11, sp, lr}
    mov     pc, r0

    .section .data
    .align  2
saved:
    .space  14 * 4
