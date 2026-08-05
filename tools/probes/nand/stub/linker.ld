/*
 * Loaded to 0x48000240 via USB boot; returns to the bootrom.
 *
 *   0x48000200 .. 0x4800023F  USB EP3 DMA target, unusable
 *   0x48000240 .. 0x480005FF  stub code
 *   0x48000600 .. 0x4800067F  result header
 *   0x48000700 .. 0x480008FF  512-byte read scratch
 *   0x48000A00 .. 0x48000BFF  L2 buffer 5, where the NF DMA actually lands
 *   0x48000C00 .. 0x48000DFF  captured prefixes, 4 x 128 bytes
 *   0x48000E70 .. 0x48000FFC  bootrom stack, must stay clear
 *   0x4800157C                stub stack top
 */

ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000240, LENGTH = 0x3C0
}

SECTIONS
{
    .text : {
        *(.text.entry)
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

    ASSERT(_stub_end <= 0x48000600, "stub overflows into the result header")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
