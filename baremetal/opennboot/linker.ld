ENTRY(_start)

MEMORY
{
    DDR (rwx) : ORIGIN = 0x30000000, LENGTH = 0x36000
}

_stack_top = 0x30036000;

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

    /* The bootrom loads pages 1..page_count of block 0. page_count is one
     * byte in the image header, and the payload must stay inside block 0.
     * That caps it at 63 pages on a device with 64 pages per block. */
    ASSERT(_payload_end - 0x30000000 <= 63 * 2048, "payload exceeds 63 pages")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
