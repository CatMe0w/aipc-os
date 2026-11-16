    .section .rodata
    .align  2
    .global eboot_resident_start
    .global eboot_resident_end
    .global nk_resident_start
    .global nk_resident_end
eboot_resident_start:
    .incbin "residents/eboot/build/eboot.bin"
eboot_resident_end:
    .align  2
nk_resident_start:
    .incbin "residents/nk/build/nk.bin"
nk_resident_end:
