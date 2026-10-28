#include "layout.h"

ENTRY(nk_entry)

SECTIONS
{
    . = CFW_PHYS;

    .text : {
        KEEP(*(.header))
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* cfw_enable and the eboot stage return point are here, because the nk
     * stage clears .bss. */
    .data : {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    ASSERT(__bss_end <= CFW_PHYS + CFW_MAX, "cfw overflows CFW_MAX")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
