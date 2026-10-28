    .section .rodata
    .align  2
    .global nk_resident_start
    .global nk_resident_end
nk_resident_start:
    .incbin "residents/nk/build/nk.bin"
nk_resident_end:
