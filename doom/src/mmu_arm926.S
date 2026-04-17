/*
 * Temporary ARM926EJ-S bring-up sequence for a stable framebuffer path while
 * the runtime LCD configuration is still being reverse engineered.
 *
 * Keep MMU and D-cache disabled. Enable only I-cache and write buffer.
 * The caller still passes r0 for compatibility with the existing C glue.
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

    mrc     p15, 0, r2, c1, c0, 0      @ read control register
    bic     r2, r2, #0x0002            @ disable alignment faults
    bic     r2, r2, #0x0005            @ force MMU and D-cache off
    orr     r2, r2, #0x5000            @ enable I-cache + round-robin
    orr     r2, r2, #0x0038            @ + write buffer + 32-bit address range
    b       1f

    .align 5
1:
    mcr     p15, 0, r2, c1, c0, 0      @ enable I-cache and write buffer only
    mrc     p15, 0, r2, c1, c0, 0      @ read back to flush pipeline
    mov     pc, lr

aipc_drain_write_buffer:
    mov     r0, #0
    mcr     p15, 0, r0, c7, c10, 4     @ drain write buffer
    mov     pc, lr
