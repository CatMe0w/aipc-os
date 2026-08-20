ENTRY(_start)

_stack_top = 0x33800000;
_heap_end  = 0x33700000;

SECTIONS
{
    . = 0x33000000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.data.rel.ro*)
        *(.got .got.plt)
    }

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    }

    . = ALIGN(16);
    _heap_start = .;

    ASSERT(_heap_start < _heap_end, "image overflows the heap")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
