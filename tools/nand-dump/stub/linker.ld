/*
 * Linker script for AK7802 NAND dump stub.
 *
 * The stub is loaded to L2BUF_01 (0x48000200) by the USB boot host tool
 * and executed in place.  Code + read-only data + mutable data must all
 * fit within the L2 buffer SRAM.
 *
 * The exact size of the L2 buffer is unclear.
 * Bootrom only accesses up to 0x480017FF (6 KB), so we use that as the
 * conservative upper bound.  If testing reveals more SRAM, the regions
 * below can be extended.
 *
 * Memory layout (0x48000000 .. 0x480017FF):
 *
 *   0x48000000 .. 0x480001FF  L2BUF_00  (512 B) - NAND DMA / USB TX staging
 *   0x48000200 .. 0x48000DFF  stub code (3 KB)
 *   0x48000E00 .. 0x48000FFF  NAND read temp buffer (512 B)
 *   0x48001000 .. 0x480017F0  stack (2 KB)
 */

ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000200, LENGTH = 0xC00
}

SECTIONS
{
    .text : {
        *(.text.entry)      /* _start must be first */
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > L2BUF

    .data : {
        *(.data .data.*)
    } > L2BUF

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > L2BUF

    _stub_end = .;

    ASSERT(_stub_end <= 0x48000E00, "stub overflows into temp buffer region")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
