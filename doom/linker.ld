/*
 * Linker script for AIPC DOOM bare-metal payload.
 *
 * The payload is uploaded to DDR by boot.sh and executed via the bootrom
 * EXECUTE opcode. DOOM1.WAD is uploaded separately to DDR_WAD_BASE.
 *
 * DDR is 64 MB: 0x30000000 - 0x33FFFFFF.
 * Framebuffer is carved out near the top: 0x33ed3c00 - 0x33f8f400 (0xBB800 bytes, RGB565).
 *
 * Memory layout:
 *   0x30000000  doom.bin (code + rodata + data + BSS)
 *   0x30000000 + _bss_end  heap (grows up)
 *   0x307E0000  stack bottom  (128 KB, grows down)
 *   0x30800000  stack top
 *   0x30900000  DOOM1.WAD (~4 MB, uploaded by boot.sh)
 */

STACK_SIZE = 0x20000;  /* 128 KB */

ENTRY(_start)

MEMORY
{
    DDR   (rwx) : ORIGIN = 0x30000000, LENGTH = 0x800000  /* 8 MB: code/data/heap/stack */
    STACK  (rw) : ORIGIN = 0x307E0000, LENGTH = STACK_SIZE
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
    _heap_end   = 0x307E0000;

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
