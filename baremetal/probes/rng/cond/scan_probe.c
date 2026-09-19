/*
 * Runs the address scan of anyka-l2rng and reports every address instead of
 * stopping at the first one that passes. See the README.
 *
 * The loop below matches l2rng_flip_permille() read for read. A close copy
 * would answer a different question.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define NC_BASE       0x48001580u
#define NC_END        0x48002000u
#define NC_WORDS      ((NC_END - NC_BASE) / 4u)      /* 672 */

/* Matches SCAN_SAMPLES and LIVE_FLIP_PERMILLE in the driver. */
#define SCAN_SAMPLES  256u
#define LIVE_PERMILLE 10u

#define HEADER_BASE   0x32008000u
#define RESULT_BASE   0x32008100u

#define MAGIC         0x53434E31u                    /* SCN1 */
#define VERSION       1u

static uint32_t flip_permille(volatile uint32_t *src, uint32_t n)
{
    uint32_t flips[32];
    uint32_t prev = *src;
    uint32_t best = 0;
    uint32_t i, bit;

    for (bit = 0; bit < 32u; bit++)
        flips[bit] = 0;

    for (i = 1; i < n; i++) {
        uint32_t cur = *src;
        uint32_t diff = cur ^ prev;

        for (bit = 0; bit < 32u; bit++) {
            if (diff & (1u << bit))
                flips[bit]++;
        }
        prev = cur;
    }

    for (bit = 0; bit < 32u; bit++) {
        if (flips[bit] > best)
            best = flips[bit];
    }

    return best * 1000u / (n - 1u);
}

void stub_main(void)
{
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)RESULT_BASE;
    uint32_t first_pass = NC_WORDS;
    uint32_t live = 0;
    uint32_t dead = 0;
    uint32_t i;

    hdr[0] = 0u;
    hdr[1] = VERSION;
    hdr[2] = NC_WORDS;
    hdr[3] = SCAN_SAMPLES;
    hdr[4] = LIVE_PERMILLE;
    hdr[5] = NC_BASE;
    hdr[6] = RESULT_BASE;

    for (i = 0; i < NC_WORDS; i++) {
        uint32_t p = flip_permille(
            (volatile uint32_t *)(uintptr_t)(NC_BASE + i * 4u),
            SCAN_SAMPLES);

        out[i] = p;
        if (p >= LIVE_PERMILLE) {
            live++;
            if (first_pass == NC_WORDS)
                first_pass = i;
        }
        if (p == 0u)
            dead++;
    }

    hdr[7] = first_pass;      /* word offset the driver would pick */
    hdr[8] = live;            /* addresses that pass the rule */
    hdr[9] = dead;            /* addresses with no bit changing at all */
    hdr[10] = out[0];         /* rate at the first address */
    hdr[0] = MAGIC;
}
