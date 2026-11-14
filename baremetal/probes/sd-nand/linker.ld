ENTRY(_start)
_stack_top = 0x320F0000;
SECTIONS
{
    . = 0x32000000;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    .data : { *(.data .data.*) }
    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    }
    ASSERT(_bss_end <= 0x32008000, "image reaches the result block")
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note.*) }
}
