ENTRY(_start)

MEMORY
{
    DDR (rwx) : ORIGIN = 0x32000000, LENGTH = 0x100000
}

_stack_top = 0x32100000;

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > DDR

    .data : {
        *(.data .data.*)
    } > DDR

    . = ALIGN(4);
    _payload_end = .;

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > DDR

    _ipl_end = .;

    ASSERT(_payload_end - 0x32000000 <= 0x64000, "ipl payload exceeds nboot 0x64000 load window")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
