/*
 * AK7802 host-driven NAND dump stub entry point
 *
 * Loaded to 0x48000240 via USB boot mode.
 *
 * The bootrom's EXECUTE handler branches here as a function call
 * (MOV LR, PC; LDR PC, [addr]). The stub MUST return (MOV PC, LR
 * or BX LR) so the bootrom resumes its USB command loop, allowing
 * the host to read back results via UPLOAD.
 *
 * SP = 0x4800157C: same value the bootrom uses before SPI/NAND
 * probe. Proven safe (bootrom_entry offset 0x64).
 */

    .section .text.entry, "ax"
    .arm
    .global _start

_start:
    /* Save bootrom's return address before bl clobbers LR. */
    mov     r4, lr
    ldr     sp, =0x4800157C
    bl      stub_main
    mov     pc, r4          /* return to bootrom */
