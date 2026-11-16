#pragma once

/* EBOOT maps the AHB peripherals at 0xA8000000 and SYSCTRL at 0xA8100000,
 * both uncached. */
#define MMIO_VA(pa)  ((pa) >= 0x20000000u ? (pa) - 0x20000000u + 0xA8000000u \
                                          : (pa) - 0x08000000u + 0xA8100000u)
