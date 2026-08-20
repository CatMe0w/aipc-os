/*
 * Image base 0x33000000, stack at 0x33800000. Define __base to relocate it.
 */

ENTRY(_start)

_stack_top = 0x33800000;

SECTIONS
{
    . = DEFINED(__base) ? __base : 0x33000000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
