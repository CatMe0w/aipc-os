    .section .rodata
    .align  2

    .global opennboot_start
    .global opennboot_end
opennboot_start:
    .incbin OPENNBOOT_BIN
opennboot_end:
