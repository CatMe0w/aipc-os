    .section .rodata
    .align  2
    .global resident_start
    .global resident_end
resident_start:
    .incbin RESIDENT_BIN
resident_end:
