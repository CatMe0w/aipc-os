ENTRY(_start)

SECTIONS
{
    . = STUB_BASE;

    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss  : {
        *(.bss .bss.*)
        *(COMMON)
    }

    _stub_end = .;

    ASSERT(_stub_end <= STUB_LIMIT, "stub overflows its window")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
