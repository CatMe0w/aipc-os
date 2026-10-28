#include "layout.h"

SECTIONS
{
    . = NK_RESIDENT_VA;

    .text : {
        KEEP(*(.text.hooks))
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* The flat image leaves out .bss. cfw_nk_resident_install() clears the
     * whole window before it copies the image. */
    .data : {
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    _image_end = .;

    ASSERT(_image_end <= NK_RESIDENT_VA + NK_RESIDENT_MAX, "nk resident overflows its window")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
