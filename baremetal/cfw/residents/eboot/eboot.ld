#include "layout.h"

SECTIONS
{
    . = RESIDENT_VA;

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    _image_end = .;

    ASSERT(_image_end <= RESIDENT_VA + RESIDENT_MAX, "eboot resident overflows its window")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
