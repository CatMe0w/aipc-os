ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000240, LENGTH = 0xC30
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

    ASSERT(_stub_end <= 0x48000E70, "stub overflows into bootrom return stack area")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
