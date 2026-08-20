ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000240, LENGTH = 0x1C0
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

    ASSERT(_stub_end <= 0x48000400, "stub overflows into data output area")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
