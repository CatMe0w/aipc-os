    .section .rodata
    .align  2

    .global cfw_start
    .global cfw_end
cfw_start:
    .incbin CFW_BIN
cfw_end:
