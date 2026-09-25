SECTIONS
{
    . = 0x80198000;

    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* The flat image leaves out .bss. resident_install() clears the whole
     * window before it copies the image. */
    .data : {
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    _image_end = .;

    ASSERT(_image_end <= 0x801A0000, "resident image overflows its window")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
