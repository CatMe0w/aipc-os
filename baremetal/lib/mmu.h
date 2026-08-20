#pragma once
#include <stdint.h>

/*
 * ARM926EJ-S section table, 1 MB per entry, identity mapped.
 *
 * The table code is shared, the map is not. Each image decides what to cache.
 * A section that one image must keep uncached for a hardware reason can hold
 * another image's code.
 *
 * Call mmu_reset(), then one mmu_map() per region, then mmu_start().
 */

#define MMU_SECTION_SIZE  0x00100000u

/* MMIO, and any memory that a DMA master reads without the core's knowledge. */
#define MMU_DEVICE     0x00000C12u
/* Writes reach memory, but the write buffer can reorder them. Readable after a
 * hang without a cache clean. */
#define MMU_BUFFERED   (MMU_DEVICE | 0x00000004u)
#define MMU_CACHED     (MMU_BUFFERED | 0x00000008u)

void mmu_reset(void);

/* Maps size bytes from base, rounded up to whole sections. */
void mmu_map(uint32_t base, uint32_t size, uint32_t flags);

void mmu_start(void);

/* Cleans and invalidates the caches, then turns the MMU, the caches and the
 * write buffer off. Every payload that we hand off to expects that state. */
void mmu_cache_disable(void);

void drain_write_buffer(void);
