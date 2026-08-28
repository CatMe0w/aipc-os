ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

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
    } > PROBE :text

    .data : {
        *(.data .data.*)
    } > PROBE :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > PROBE :data

    _stub_end = .;

    ASSERT(_stub_end <= 0x32008000, "stub overlaps result block")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
