#pragma once
#include <stdint.h>

/*
 * Register access by physical address. An image that runs under another MMU
 * mapping defines MMIO_VA() before it includes this header, and the macro
 * then translates each address.
 */
#ifndef MMIO_VA
#define MMIO_VA(pa)  (pa)
#endif

#define MMIO_PTR(pa)  ((volatile uint32_t *)(uintptr_t)MMIO_VA(pa))
#define REG32(pa)     (*MMIO_PTR(pa))
