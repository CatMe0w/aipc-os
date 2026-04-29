/*
 * ARM926EJ-S MMU/cache bring-up.
 *
 * The caller passes r0 = L1 translation table base. The table is identity
 * mapped; framebuffer/MMIO attributes are selected by the C table builder.
 */

    .section .text, "ax"
    .arm
    .align 5
    .global aipc_enable_mmu_cache
    .global aipc_drain_write_buffer

aipc_enable_mmu_cache:
    mov     r1, #0
    mcr     p15, 0, r1, c7, c7, 0      @ invalidate I+D caches
    mcr     p15, 0, r1, c7, c10, 4     @ drain write buffer
    mcr     p15, 0, r1, c8, c7, 0      @ invalidate unified TLB

    mcr     p15, 0, r0, c2, c0, 0      @ TTBR0 = L1 table
    mov     r1, #3
    mcr     p15, 0, r1, c3, c0, 0      @ domain 0 manager

    mrc     p15, 0, r2, c1, c0, 0      @ read control register
    bic     r2, r2, #0x0002            @ disable alignment faults
    orr     r2, r2, #0x0005            @ prepare MMU + D-cache
    orr     r2, r2, #0x1000            @ + I-cache
    orr     r2, r2, #0x0038            @ + write buffer + 32-bit address range
    b       1f

    .align 5
1:
    mcr     p15, 0, r2, c1, c0, 0      @ enable MMU/cache
    mrc     p15, 0, r2, c1, c0, 0      @ read back to flush pipeline
    mov     pc, lr

aipc_drain_write_buffer:
    mov     r0, #0
    mcr     p15, 0, r0, c7, c10, 4     @ drain write buffer
    mov     pc, lr
