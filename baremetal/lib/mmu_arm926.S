/*
 * ARM926EJ-S MMU and cache control.
 */

    .section .text, "ax"
    .arm
    .align 5
    .global mmu_enable
    .global mmu_cache_disable
    .global drain_write_buffer

/* r0 = L1 table base. Domain 0 is manager, so the hardware ignores the section
 * AP bits. */
mmu_enable:
    mov     r1, #0
    mcr     p15, 0, r1, c7, c7, 0       @ invalidate I+D caches
    mcr     p15, 0, r1, c7, c10, 4      @ drain write buffer
    mcr     p15, 0, r1, c8, c7, 0       @ invalidate TLB

    mcr     p15, 0, r0, c2, c0, 0       @ TTBR0
    mov     r1, #3
    mcr     p15, 0, r1, c3, c0, 0       @ domain 0 = manager

    mrc     p15, 0, r2, c1, c0, 0
    bic     r2, r2, #0x0002             @ no alignment faults
    orr     r2, r2, #0x000D             @ MMU, D-cache, write buffer
    orr     r2, r2, #0x0030             @ 32-bit address and exception range
    orr     r2, r2, #0x1000             @ I-cache
    b       1f

    .align 5
1:
    mcr     p15, 0, r2, c1, c0, 0
    mrc     p15, 0, r2, c1, c0, 0       @ read back to settle the pipeline
    mov     pc, lr

    .align 5
mmu_cache_disable:
    mov     r0, #0
    mcr     p15, 0, r0, c7, c10, 4      @ drain write buffer
1:  mrc     p15, 0, r15, c7, c14, 3     @ test, clean and invalidate D-cache
    bne     1b
    mcr     p15, 0, r0, c7, c10, 4

    mrc     p15, 0, r1, c1, c0, 0
    bic     r1, r1, #0x000D             @ MMU, D-cache, write buffer off
    bic     r1, r1, #0x1000             @ I-cache off
    mcr     p15, 0, r1, c1, c0, 0
    mrc     p15, 0, r1, c1, c0, 0

    mcr     p15, 0, r0, c7, c7, 0       @ invalidate I+D caches
    mcr     p15, 0, r0, c8, c7, 0       @ invalidate TLB
    mov     pc, lr

drain_write_buffer:
    mov     r0, #0
    mcr     p15, 0, r0, c7, c10, 4
    mov     pc, lr
