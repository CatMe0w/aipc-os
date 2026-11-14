/* block0.c against a NAND block in memory. */

#include <stdio.h>
#include <string.h>
#include "block0.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
        failures++;                                             \
    }                                                           \
} while (0)

static uint8_t nand[BLOCK0_BYTES];
static uint32_t erases, programs;
static int erase_rc;
static uint32_t stuck_at;   /* a byte that every program leaves at 0x00 */
static int stuck;

int nand_erase_block(uint32_t page, uint32_t *status)
{
    (void)page;
    *status = erase_rc ? 0xE1u : 0xE0u;
    if (erase_rc)
        return erase_rc;
    memset(nand, 0xFF, sizeof(nand));
    erases++;
    return 0;
}

int nand_program_page(uint32_t page, const void *src, uint32_t len,
                      uint32_t *status)
{
    memcpy(nand + page * NAND_PAGE_RAW, src, len);
    if (stuck)
        nand[stuck_at] = 0x00;
    programs++;
    *status = 0xE0u;
    return 0;
}

int nand_read_pages_raw(uint32_t page, uint32_t count, void *dst, uint32_t *done)
{
    memcpy(dst, nand + page * NAND_PAGE_RAW, count * NAND_PAGE_RAW);
    *done = count;
    return 0;
}

static uint8_t current[BLOCK0_BYTES], image[BLOCK0_BYTES], back[BLOCK0_BYTES];
static uint8_t payload[BLOCK0_PAYLOAD_MAX + 1u];

/* The header page of the stock bootloader: six pages of four chunks. */
static void stock(uint8_t *dst)
{
    memset(dst, 0xFF, BLOCK0_BYTES);
    memset(dst, 0x5A, NAND_PAGE_DATA);
    memcpy(dst + 4, "ANYKA382", 8);
    dst[0x0C] = 4;
    dst[0x0D] = 6;
    for (uint32_t p = 1; p <= 6; p++)
        memset(dst + p * NAND_PAGE_RAW, 0x11 * p, NAND_PAGE_RAW);
}

static void reset(void)
{
    erases = programs = 0;
    erase_rc = 0;
    stuck = 0;
    stock(current);
    memcpy(nand, current, sizeof(nand));
    for (uint32_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i * 7u + 1u);
}

static void test_check(void)
{
    reset();
    CHECK(block0_check(current) == 0, "the stock header fails the check");

    current[4] = 'X';
    CHECK(block0_check(current) == BLOCK0_ESIGNATURE, "signature");
    reset();
    current[0x0C] = 1;
    CHECK(block0_check(current) == BLOCK0_EPAGESIZE, "page size");
    reset();
    current[0x0D] = 64;
    CHECK(block0_check(current) == BLOCK0_EPAGES, "page count 64");
    current[0x0D] = 63;
    CHECK(block0_check(current) == 0, "page count 63");
}

static void test_build(void)
{
    static const uint32_t sizes[] = { 1, 2047, 2048, 2049, 6768,
                                      BLOCK0_PAYLOAD_MAX };

    for (uint32_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        uint32_t len = sizes[s], pages = (len + 2047u) / 2048u;
        int ok = 1;

        reset();
        CHECK(block0_build(current, payload, len, image) == 0, "build %u", len);
        CHECK(image[0x0D] == pages, "page count %u for %u bytes", image[0x0D], len);
        image[0x0D] = current[0x0D];
        CHECK(!memcmp(image, current, NAND_PAGE_DATA), "header page changed");

        for (uint32_t p = 0; p < BLOCK0_PAGES; p++) {
            const uint8_t *page = image + p * NAND_PAGE_RAW;

            for (uint32_t c = p ? 0 : NAND_PAGE_DATA; c < NAND_PAGE_RAW; c++) {
                uint32_t at = (p - 1u) * NAND_PAGE_DATA + c;
                uint8_t want = p && c < NAND_PAGE_DATA && at < len ? payload[at] : 0xFF;

                ok &= page[c] == want;
            }
        }
        CHECK(ok, "layout of a %u byte payload", len);
    }

    reset();
    CHECK(block0_build(current, payload, 0, image) == BLOCK0_ETOOLARGE, "empty");
    CHECK(block0_build(current, payload, BLOCK0_PAYLOAD_MAX + 1u, image) ==
          BLOCK0_ETOOLARGE, "one byte too large");
    current[4] = 0;
    CHECK(block0_build(current, payload, 6768, image) == BLOCK0_ESIGNATURE,
          "build from a block without the signature");
}

static void test_write(void)
{
    struct block0_result res;

    reset();
    block0_build(current, payload, 6768, image);
    CHECK(block0_write(0, image, back, &res) == 0, "write");
    CHECK(erases == 1 && programs == 5, "%u erases, %u programs", erases, programs);

    /* The spare bytes of a backup are not written. */
    reset();
    memset(nand, 0, sizeof(nand));
    CHECK(block0_write(0, current, back, &res) == 0, "write of the stock image");
    CHECK(programs == 7, "%u programs", programs);
    CHECK(!memcmp(nand, current, NAND_PAGE_DATA) &&
          nand[NAND_PAGE_RAW + NAND_PAGE_DATA] == 0xFF &&
          nand[NAND_PAGE_RAW] == 0x11, "contents after the write");

    reset();
    stuck = 1;
    stuck_at = 3u * NAND_PAGE_RAW + 0x123u;
    block0_build(current, payload, 6768, image);
    CHECK(block0_write(0, image, back, &res) == BLOCK0_EVERIFY, "stuck byte");
    CHECK(res.step == BLOCK0_COMPARE && res.page == 3 && res.column == 0x123 &&
          res.got == 0 && res.want == image[stuck_at],
          "step %u page %u column %#x", res.step, res.page, res.column);

    reset();
    erase_rc = NAND_ESTATUS;
    CHECK(block0_write(0, current, back, &res) == NAND_ESTATUS, "erase failure");
    CHECK(res.step == BLOCK0_ERASE && res.status == 0xE1 && programs == 0,
          "step %u status %#x", res.step, res.status);

    reset();
    current[4] = 0;
    CHECK(block0_write(0, current, back, &res) == BLOCK0_ESIGNATURE, "bad image");
    CHECK(res.step == BLOCK0_CHECK && erases == 0, "an invalid image was written");
}

int main(void)
{
    test_check();
    test_build();
    test_write();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_block0: ok\n");
    return 0;
}
