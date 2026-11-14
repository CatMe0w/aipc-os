#include "block0.h"

/* Header page fields, see docs/bootrom/nand-boot.md */
#define HDR_SIGNATURE   0x04u
#define HDR_CHUNKS      0x0Cu   /* 512-byte chunks in a page */
#define HDR_PAGE_COUNT  0x0Du   /* pages of the bootloader, from page 1 */

static const uint8_t signature[8] = { 'A', 'N', 'Y', 'K', 'A', '3', '8', '2' };

int block0_check(const uint8_t *image)
{
    for (uint32_t i = 0; i < sizeof(signature); ++i)
        if (image[HDR_SIGNATURE + i] != signature[i])
            return BLOCK0_ESIGNATURE;
    if (image[HDR_CHUNKS] * 512u != NAND_PAGE_DATA)
        return BLOCK0_EPAGESIZE;
    if (image[HDR_PAGE_COUNT] > BLOCK0_PAGES - 1u)
        return BLOCK0_EPAGES;
    return 0;
}

int block0_build(const uint8_t *current, const void *payload, uint32_t len,
                 uint8_t *image)
{
    const uint8_t *in = (const uint8_t *)payload;
    int rc = block0_check(current);

    if (rc)
        return rc;
    if (len == 0u || len > BLOCK0_PAYLOAD_MAX)
        return BLOCK0_ETOOLARGE;

    for (uint32_t i = 0; i < BLOCK0_BYTES; ++i)
        image[i] = 0xFFu;
    for (uint32_t i = 0; i < NAND_PAGE_DATA; ++i)
        image[i] = current[i];
    image[HDR_PAGE_COUNT] = (uint8_t)((len + NAND_PAGE_DATA - 1u) / NAND_PAGE_DATA);

    for (uint32_t done = 0; done < len; done += NAND_PAGE_DATA) {
        uint32_t n = len - done < NAND_PAGE_DATA ? len - done : NAND_PAGE_DATA;

        image += NAND_PAGE_RAW;
        for (uint32_t i = 0; i < n; ++i)
            image[i] = in[done + i];
    }
    return 0;
}

/* The pages up to the last one that has data. */
static uint32_t pages_used(const uint8_t *image)
{
    uint32_t count = 0;

    for (uint32_t p = 0; p < BLOCK0_PAGES; ++p)
        for (uint32_t i = 0; i < NAND_PAGE_DATA; ++i)
            if (image[p * NAND_PAGE_RAW + i] != 0xFFu) {
                count = p + 1u;
                break;
            }
    return count;
}

int block0_write(uint32_t page, const uint8_t *image, uint8_t *readback,
                 struct block0_result *res)
{
    uint32_t count, done = 0;
    int rc;

    res->step = BLOCK0_CHECK;
    res->status = 0;
    res->page = 0;
    res->column = 0;
    res->got = res->want = 0;

    rc = block0_check(image);
    if (rc)
        return rc;
    count = pages_used(image);

    res->step = BLOCK0_ERASE;
    rc = nand_erase_block(page, &res->status);
    if (rc)
        return rc;

    res->step = BLOCK0_PROGRAM;
    for (uint32_t p = 0; p < count; ++p) {
        res->page = p;
        rc = nand_program_page(page + p, image + p * NAND_PAGE_RAW,
                               NAND_PAGE_DATA, &res->status);
        if (rc)
            return rc;
    }

    res->step = BLOCK0_READBACK;
    rc = nand_read_pages_raw(page, BLOCK0_PAGES, readback, &done);
    res->page = done;
    if (rc)
        return rc;

    res->step = BLOCK0_COMPARE;
    for (uint32_t p = 0; p < BLOCK0_PAGES; ++p) {
        const uint8_t *got = readback + p * NAND_PAGE_RAW;
        const uint8_t *src = image + p * NAND_PAGE_RAW;

        for (uint32_t c = 0; c < NAND_PAGE_RAW; ++c) {
            uint8_t want = c < NAND_PAGE_DATA ? src[c] : 0xFFu;

            if (got[c] != want) {
                res->page = p;
                res->column = c;
                res->got = got[c];
                res->want = want;
                return BLOCK0_EVERIFY;
            }
        }
    }
    return 0;
}
