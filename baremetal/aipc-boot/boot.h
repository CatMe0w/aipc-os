#pragma once
#include <stdint.h>

struct entry;
struct cfw_component_info;

/* Loads the kernel of e. Returns 0, or a negative code. Requires a mounted
 * card. */
int linux_load(const struct entry *e);

/* Starts the kernel that linux_load() loaded. Without a cmdline in e, it
 * passes no ATAG list (r2 = 0). */
void linux_enter(const struct entry *e) __attribute__((noreturn));

/* Turns off the caches and the MMU, then jumps to entry. */
void boot_jump(uint32_t entry) __attribute__((noreturn));

/* Calls a payload as an AAPCS function, with the MMU and the caches off.
 * When it returns, the hardware that the payload can change is set up
 * again. See README.md#payloads. */
void payload_call(uint32_t entry);

/* Sets up the L2 buffer, the LCD, the timer, the MMU and the keyboard. */
void boot_hw_init(void);

/* Loads EBOOT from NAND. NAND and SD share a pad group, and this function
 * gives the pads to the NAND controller. The caller must therefore call
 * card_init() before the next card access. With cfw set, CFW patches EBOOT
 * with the components that settings.ini enables, or their defaults. Returns
 * 0, or a negative code. */
int wince_prepare(int cfw);

/* 1 when the power_hold component of CFW keeps the device on through the
 * handoff. An enabled component either applies, or fails wince_prepare(). */
int wince_power_held(int cfw);

/* Starts the EBOOT that wince_prepare() loaded. */
void wince_enter(void) __attribute__((noreturn));

#define BOOT_ECFW  (-20)  /* cfw_error() gives the reason */

/* The message of the last BOOT_ECFW failure. */
const char *cfw_error(void);

/* The CFW components, in the order of the enable mask. */
int cfw_component_count(void);
const struct cfw_component_info *cfw_component(int i);

/* 1 when settings.ini or the default of c enables it. */
int cfw_component_on(const struct cfw_component_info *c);
