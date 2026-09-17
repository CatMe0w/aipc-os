/*
 * Same layout as the GDB stub, because openNBOOT loads this as BOOT.BIN to the
 * same address. The extra section holds the code that gets copied into L2 SRAM.
 */

ENTRY(_start)

_stack_top = 0x33800000;

SECTIONS
{
    . = 0x33000000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* Copied into L2 SRAM and run from there. Kept whole and contiguous so
     * that the PC-relative literal pool inside it survives the copy. */
    .l2text : {
        . = ALIGN(4);
        __l2text_start = .;
        *(.l2text)
        . = ALIGN(4);
        __l2text_end = .;
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
