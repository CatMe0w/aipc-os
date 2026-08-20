STACK_SIZE = 0x20000;  /* 128 KB */

ENTRY(_start)

MEMORY
{
    DDR   (rwx) : ORIGIN = 0x30200000, LENGTH = 0x800000  /* 8 MB: code/data/heap/stack */
    STACK  (rw) : ORIGIN = 0x309E0000, LENGTH = STACK_SIZE
}

SECTIONS
{
    .text : {
        *(.text.entry)      /* _start must be first */
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > DDR

    .data : {
        *(.data .data.*)
    } > DDR

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > DDR

    _heap_start = .;
    _heap_end   = 0x309E0000;

    .stack (NOLOAD) : {
        _stack_bottom = .;
        . += STACK_SIZE;
        _stack_top = .;
    } > STACK

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
