ENTRY(_start)

_stack_top = 0x33800000;
/* The stack below _stack_top gets the 1 MB above _image_limit. */
_image_limit = 0x33700000;

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

    ASSERT(. <= _image_limit, "image overlaps the stack")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
