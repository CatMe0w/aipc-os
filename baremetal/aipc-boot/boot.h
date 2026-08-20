#pragma once
#include <stdint.h>

/* Each of these hands off and never returns on success. On failure it returns
 * a negative code and leaves the machine usable, so the menu can report it.
 *
 * boot_linux() and boot_gdbstub() need a successful sd_init().
 * boot_wince() takes the SD pads back for the NAND controller. */
int boot_linux(void);
int boot_gdbstub(void);
int boot_wince(void);
