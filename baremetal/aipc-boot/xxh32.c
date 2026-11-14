#include <string.h>
#include "xxh32.h"

#define P1 2654435761u
#define P2 2246822519u
#define P3 3266489917u
#define P4 668265263u
#define P5 374761393u

static uint32_t rotl(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void stripe(struct xxh32 *x, const uint8_t *p)
{
    for (int i = 0; i < 4; i++)
        x->v[i] = rotl(x->v[i] + le32(p + 4 * i) * P2, 13) * P1;
}

void xxh32_init(struct xxh32 *x)
{
    x->v[0] = P1 + P2;
    x->v[1] = P2;
    x->v[2] = 0;
    x->v[3] = 0u - P1;
    x->total = 0;
    x->fill = 0;
}

void xxh32_update(struct xxh32 *x, const void *data, uint32_t len)
{
    const uint8_t *p = data;

    x->total += len;
    if (x->fill) {
        uint32_t n = 16u - x->fill < len ? 16u - x->fill : len;

        memcpy(x->pending + x->fill, p, n);
        x->fill += n;
        p += n;
        len -= n;
        if (x->fill < 16u)
            return;
        stripe(x, x->pending);
        x->fill = 0;
    }
    for (; len >= 16u; p += 16, len -= 16u)
        stripe(x, p);
    memcpy(x->pending, p, len);
    x->fill = len;
}

uint32_t xxh32_digest(const struct xxh32 *x)
{
    const uint8_t *p = x->pending;
    uint32_t left = x->fill;
    uint32_t h;

    if (x->total >= 16u)
        h = rotl(x->v[0], 1) + rotl(x->v[1], 7) + rotl(x->v[2], 12) +
            rotl(x->v[3], 18);
    else
        h = P5;
    h += x->total;

    for (; left >= 4u; p += 4, left -= 4u)
        h = rotl(h + le32(p) * P3, 17) * P4;
    for (; left; p++, left--)
        h = rotl(h + *p * P5, 11) * P1;

    h ^= h >> 15;
    h *= P2;
    h ^= h >> 13;
    h *= P3;
    h ^= h >> 16;
    return h;
}
