/*
 * Linker script for AIPC NAND dump stubs.
 *
 * Loaded to 0x48000240 by the USB boot host tool and executed in place.
 * NOT 0x48000200: the USB EP3 RX hardware DMA overwrites
 * 0x48000200-0x4800023F with every USB packet.
 *
 * L2 buffer is 5.375 KB (5504 bytes) physical SRAM at 0x48000000-0x4800157F.
 * Addresses 0x48001580-0x48001FFF are not connected (reads return noise).
 * Accesses from 0x48002000 onward alias back with a 0x2000 period.
 *
 * Several regions are hardware-managed and must be avoided:
 *   0x0000-0x003F  USB EP2 bulk IN staging (L2BUF_00)
 *   0x0200-0x023F  USB EP3 bulk OUT DMA target (L2BUF_01)
 *   0x1000-0x10FF  UART buffer / L2 control region
 *
 * Memory layout (post-EXECUTE):
 *
 *   0x48000000 .. 0x4800003F  USB TX staging (64 B, HW-managed)
 *   0x48000040 .. 0x4800007F  Parameter / result block
 *   0x48000240 .. 0x48000BFF  Stub code / rodata / bss
 *   0x48000C00 .. 0x48000E6F  I/O scratch buffer
 *   0x48000E70 .. 0x48000FFC  Bootrom call chain stack (preserved for return)
 *   0x48001100 .. 0x4800157B  Stack (~1.1 KB, used by stub)
 */

ENTRY(_start)

MEMORY
{
    L2BUF (rwx) : ORIGIN = 0x48000240, LENGTH = 0x9C0
}

SECTIONS
{
    .text : {
        *(.text.entry)      /* _start must be first */
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

    ASSERT(_stub_end <= 0x48000C00, "stub overflows into scratch buffer")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.comment)
        *(.note.*)
    }
}
