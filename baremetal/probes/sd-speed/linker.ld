ENTRY(_start)
_stack_top = 0x31100000;
SECTIONS
{
    . = 0x31000000;
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
    ASSERT(_bss_end <= 0x310F0000, "image reaches the stack")
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note.*) }
}
