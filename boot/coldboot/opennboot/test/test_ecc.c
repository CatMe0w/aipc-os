/* Fixtures: six single-bit-error chunks from the 1.58.2 device, four confirmed
 * on hardware, two predicted. */

#include <stdio.h>
#include <string.h>
#include "../ecc.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
        failures++;                                             \
    }                                                           \
} while (0)

static const struct {
    uint32_t reg;
    uint32_t byte;
    uint8_t  mask;
    const char *origin;
} fixtures[] = {
    { 0x00008167u, 361u, 0x20u, "page 55653 chunk 3, live" },
    { 0x000041FBu, 270u, 0x10u, "page 57041 chunk 0, live" },
    { 0x0000118Au, 114u, 0x40u, "page 57977 chunk 3, live" },
    { 0x00010019u, 277u, 0x01u, "page 61153 chunk 1, live" },
    { 0x0004016Bu, 490u, 0x10u, "page 60094 chunk 2, predicted" },
    { 0x00040165u, 488u, 0x01u, "page 62825 chunk 2, predicted" },
};

static const int seg_base[5] = { -112, 16, 144, 272, 400 };

/* Independent inverse, written from the closed form rather than from ecc.c. */
static int locate_to_reg(uint32_t byte, uint8_t mask, uint32_t *reg_out)
{
    int bit_msb = 0;
    while ((mask & (1u << (7 - bit_msb))) == 0)
        bit_msb++;
    int off = (int)byte * 8 + bit_msb;

    for (int pair = 0; pair < 5; pair++) {
        int v = off - 8 * seg_base[pair] + 4;
        if (v < 0)
            continue;
        int parity = v & 1;
        int pos    = v >> 1;
        if (pos > 511)
            continue;
        int i = 2 * pair + parity;
        *reg_out = ((1u << i) << 9) | (uint32_t)pos;
        return 0;
    }
    return -1;
}

static void test_fixtures(void)
{
    for (size_t k = 0; k < sizeof(fixtures) / sizeof(fixtures[0]); k++) {
        uint32_t byte = 0xFFFFFFFFu;
        uint8_t  mask = 0;

        int rc = ecc_locate(fixtures[k].reg, &byte, &mask);
        CHECK(rc == 0, "%s: locate rejected %#010x", fixtures[k].origin,
              fixtures[k].reg);
        CHECK(byte == fixtures[k].byte && mask == fixtures[k].mask,
              "%s: %#010x -> byte %u mask %#04x, want byte %u mask %#04x",
              fixtures[k].origin, fixtures[k].reg, byte, mask,
              fixtures[k].byte, fixtures[k].mask);
    }
}

static void test_apply_flips_one_bit(void)
{
    for (size_t k = 0; k < sizeof(fixtures) / sizeof(fixtures[0]); k++) {
        uint8_t clean[512], data[512], tag[4] = { 0, 0, 0, 0 };

        for (int i = 0; i < 512; i++)
            clean[i] = (uint8_t)(i * 7 + 3);
        memcpy(data, clean, sizeof(data));
        data[fixtures[k].byte] ^= fixtures[k].mask;   /* inject the flip */

        uint32_t regs[4] = { fixtures[k].reg, 0, 0, 0 };
        int rc = ecc_apply(regs, 4, data, tag, sizeof(tag));

        CHECK(rc == 0, "%s: apply failed", fixtures[k].origin);
        CHECK(memcmp(data, clean, sizeof(data)) == 0,
              "%s: chunk not restored", fixtures[k].origin);
    }
}

static void test_tag_region(void)
{
    uint8_t data[512], tag[4] = { 0, 0, 0, 0 };
    memset(data, 0, sizeof(data));

    uint32_t reg;
    CHECK(locate_to_reg(513, 0x08u, &reg) == 0, "no register encodes byte 513");

    uint32_t regs[1] = { reg };
    CHECK(ecc_apply(regs, 1, data, tag, sizeof(tag)) == 0,
          "apply rejected a tag-region correction");
    CHECK(tag[1] == 0x08u, "tag byte not flipped, got %#04x", tag[1]);

    /* One past the tag scratch must be refused rather than written. */
    CHECK(locate_to_reg(516, 0x01u, &reg) == 0, "no register encodes byte 516");
    regs[0] = reg;
    CHECK(ecc_apply(regs, 1, data, tag, sizeof(tag)) == -1,
          "apply accepted a correction past the tag scratch");
}

static void test_rejections(void)
{
    uint8_t data[512], tag[4];
    uint32_t byte;
    uint8_t  mask;

    CHECK(ecc_locate(0x00000000u, &byte, &mask) == -1,
          "zero selector accepted");
    CHECK(ecc_locate(0x00006000u, &byte, &mask) == -1,
          "two-bit selector accepted");
    CHECK(ecc_locate(0x00000200u, &byte, &mask) == -1,
          "segment 0 low position accepted (decodes negative)");

    uint32_t empty[4] = { 0, 0, 0, 0 };
    CHECK(ecc_apply(empty, 4, data, tag, sizeof(tag)) == -1,
          "empty register set accepted");
}

static void test_round_trip(void)
{
    int checked = 0;

    for (int i = 0; i < 10; i++) {
        for (int pos = 0; pos <= 511; pos++) {
            uint32_t reg = ((1u << i) << 9) | (uint32_t)pos;
            uint32_t byte;
            uint8_t  mask;

            if (ecc_locate(reg, &byte, &mask) != 0)
                continue;
            if (byte > 515u)
                continue;

            uint32_t back;
            CHECK(locate_to_reg(byte, mask, &back) == 0,
                  "byte %u mask %#04x has no encoding", byte, mask);
            CHECK(back == reg, "round trip %#010x -> byte %u mask %#04x -> %#010x",
                  reg, byte, mask, back);
            checked++;
        }
    }

    CHECK(checked > 4000, "round trip covered only %d positions", checked);
    printf("round trip covered %d in-range positions\n", checked);
}

int main(void)
{
    test_fixtures();
    test_apply_flips_one_bit();
    test_tag_region();
    test_rejections();
    test_round_trip();

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all ecc tests passed\n");
    return 0;
}
