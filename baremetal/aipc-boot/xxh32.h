#pragma once
#include <stdint.h>

/* xxHash32 with seed 0, the content checksum of an LZ4 frame. */
struct xxh32 {
    uint32_t v[4];
    uint32_t total;
    uint32_t fill;
    uint8_t pending[16];
};

void xxh32_init(struct xxh32 *x);
void xxh32_update(struct xxh32 *x, const void *data, uint32_t len);
uint32_t xxh32_digest(const struct xxh32 *x);
