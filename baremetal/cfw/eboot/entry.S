    .section .text, "ax"
    .arm
    .global eboot_stage
    .global cfw_eboot_return

/* r0 = args, r1 = result */
eboot_stage:
    ldr     r2, =__bss_start
    ldr     r3, =__bss_end
    mov     ip, #0
1:  cmp     r2, r3
    strlo   ip, [r2], #4
    blo     1b
    ldr     ip, =caller
    stmia   ip, {r4-r11, sp, lr}
    bl      eboot_main
    ldr     ip, =caller
    ldmia   ip, {r4-r11, sp, lr}
    bx      lr

cfw_eboot_return:
    mvn     r0, #0
    ldr     ip, =caller
    ldmia   ip, {r4-r11, sp, lr}
    bx      lr

    .section .data
    .align  2
caller:
    .space  10 * 4
