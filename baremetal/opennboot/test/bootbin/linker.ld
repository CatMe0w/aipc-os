ENTRY(_start)

MEMORY
{
    DDR (rwx) : ORIGIN = 0x33000000, LENGTH = 0x100000
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > DDR

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
