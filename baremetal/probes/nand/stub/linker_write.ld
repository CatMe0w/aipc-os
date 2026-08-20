/*
 * The write stub is larger than the read ones, so it takes a different slice
 * of L2. Loaded to 0x48000240 via USB boot; returns to the bootrom.
 *
 *   0x48000200 .. 0x4800023F  USB EP3 DMA target, unusable
 *   0x48000240 .. 0x480009FF  stub code
 *   0x48000A00 .. 0x48000BFF  L2 buffer 5, where the NF DMA lands
 *   0x48000C00 .. 0x48000DFF  512-byte read scratch
 *   0x48000E70 .. 0x48000FFC  bootrom stack, must stay clear
 *   0x48001100 .. 0x4800117F  result header
 *   0x4800157C                stub stack top
 */

ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000240, LENGTH = 0x7C0
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > L2BUF

    .data : { *(.data .data.*) } > L2BUF
    .bss  : { *(.bss .bss.*) *(COMMON) } > L2BUF

    _stub_end = .;
    ASSERT(_stub_end <= 0x48000A00, "stub overflows into L2 buffer 5")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
