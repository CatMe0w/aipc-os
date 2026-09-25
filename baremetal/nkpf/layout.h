#pragma once

/*
 * These windows are in the log pool below NK, where neither EBOOT nor WinCE
 * writes. The resident image starts at the top of the nkpf stack, because
 * nothing uses that stack after NK starts. The log window at 0x30180000 is
 * LOG_BASE in the Makefile.
 */
#define NKPF_PHYS           0x30190000u
#define NKPF_MAX            0x00006000u
#define NKPF_STACK_TOP      0x30198000u
#define RESIDENT_PHYS       0x30198000u
#define RESIDENT_MAX        0x00008000u

#define NK_VA_OFFSET        0x50000000u
#define NK_UNCACHED_OFFSET  0x70000000u
#define RESIDENT_VA         (RESIDENT_PHYS + NK_VA_OFFSET)

#define FB_PHYS             0x33B00000u
#define FB_WIDTH            800u
#define FB_HEIGHT           480u
