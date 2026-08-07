/*
 *   0x48000200 .. 0x4800023F  USB EP3 DMA target, unusable
 *   0x48000240 .. 0x480005FF  stub code
 *   0x48000600 .. 0x480006FF  result block
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

    ASSERT(_stub_end <= 0x48000600, "stub overflows into the result block")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
