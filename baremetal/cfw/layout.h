#pragma once
#include "cfw_abi.h"

/* In the log pool below NK, where neither EBOOT nor WinCE writes. See
 * ../README.md. */
#define CFW_STACK_TOP       CFW_U(0x3019C000)
/* The two residents use the same window. The eboot resident stops when
 * EBOOT hands off, before the nk stage installs the nk resident. */
#define RESIDENT_PHYS       CFW_U(0x3019C000)
#define RESIDENT_MAX        CFW_U(0x00004000)

/* EBOOT and WinCE both map DDR at 0x80000000. */
#define DDR_VA_OFFSET       CFW_U(0x50000000)
/* WinCE maps 128 MB at 0x80000000, but only the first 64 MB is DDR. */
#define NK_DDR_VA           0x80000000u
#define NK_DDR_END          0x84000000u
#define NK_UNCACHED_OFFSET  0x70000000u
#define RESIDENT_VA         (RESIDENT_PHYS + DDR_VA_OFFSET)

#define EBOOT_CODE_VA       0x80038000u

#define FB_PHYS             0x33B00000u
#define FB_WIDTH            800u
#define FB_HEIGHT           480u
