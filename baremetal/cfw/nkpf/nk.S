#include "layout.h"

    .section .text, "ax"
    .arm
    .global nk_entry

/* The patched EBOOT jumps here with the MMU and the caches off, and r0 holds
 * the NK entry. sp is an EBOOT VA, so it is not usable.
 *
 * EBOOT never cleans its write-back D-cache, so part of NK is only in dirty
 * lines. The clean must come before anything reads NK or writes DDR. With the
 * MMU off, it writes each line back to the physical address in its tag. */
nk_entry:
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
    ldr     sp, =CFW_STACK_TOP
    bl      nkpf_main
    ldr     ip, =saved
    ldmia   ip, {r0-r11, sp, lr}
    mov     pc, r0

    .section .data
    .align  2
saved:
    .space  14 * 4
