ENTRY(_start)

MEMORY
{
    PROBE (rwx) : ORIGIN = 0x32000000, LENGTH = 0x8000
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > PROBE

    .data : {
        *(.data .data.*)
    } > PROBE

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > PROBE

    _stub_end = .;

    ASSERT(_stub_end <= 0x32008000, "stub overlaps result block")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
