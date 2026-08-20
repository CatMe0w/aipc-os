ENTRY(_start)

MEMORY
{
    DDR (rwx) : ORIGIN = 0x30090000, LENGTH = 0x10000
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > DDR

    .data : {
        *(.data .data.*)
    } > DDR

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > DDR

    _stub_end = .;

    ASSERT(_stub_end <= 0x300a0000, "stub exceeds reserved DDR probe range")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
