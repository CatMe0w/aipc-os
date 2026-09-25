/* nkpf.bin, carried inside BOOT.BIN and copied out by boot_wince(). */

    .section .rodata
    .align  2

    .global nkpf_start
    .global nkpf_end
nkpf_start:
    .incbin NKPF_BIN
nkpf_end:
