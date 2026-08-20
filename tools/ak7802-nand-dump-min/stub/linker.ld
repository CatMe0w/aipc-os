/*
 * Linker script for AK7802 host-driven NAND dump stub.
 *
 * Loaded to 0x48000240 via USB boot. Must be small enough to leave
 * room for the parameter block (0x48000040) and data output area
 * (0x48000400+). The stub itself lives at 0x240..0x3FF (448 bytes max).
 *
 * Memory layout (during EXECUTE, bootrom suspended):
 *
 *   0x48000000 .. 0x4800003F  (L2BUF_00, used by NF DMA internally)
 *   0x48000040 .. 0x4800007F  Parameter block (written by host)
 *   0x48000080 .. 0x4800023F  (free)
 *   0x48000240 .. 0x480003FF  Stub code (<=448 B)
 *   0x48000400 .. 0x48000BFF  Data output area (up to 2048 B)
 *   0x48001100 .. 0x4800157B  Stack (bootrom-proven)
 */

ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000240, LENGTH = 0x1C0
}

SECTIONS
{
    .text : {
        *(.text.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > L2BUF

    .data : {
        *(.data .data.*)
    } > L2BUF

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > L2BUF

    _stub_end = .;

    ASSERT(_stub_end <= 0x48000400, "stub overflows into data output area")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
