#include <stdint.h>
#include "nand.h"
#include "sd.h"

#define R       ((volatile uint32_t *)(uintptr_t)0x32008000u)
#define ORIG    ((uint32_t *)(uintptr_t)0x32100000u)
#define PATTERN ((uint32_t *)(uintptr_t)0x32200000u)
#define BACK    ((uint32_t *)(uintptr_t)0x32300000u)
#define NREF    ((uint32_t *)(uintptr_t)0x32400000u)
#define NBACK   ((uint32_t *)(uintptr_t)0x32401000u)

#define LBA       1024u
#define BLOCKS    1024u
#define WORDS     (BLOCKS * 128u)
#define NAND_PAGE 128u
#define ROUNDS    20u

enum {
    R_MAGIC, R_MODE, R_STAGE, R_SD_INIT, R_MBR, R_FIRST_PART, R_READ_ORIG,
    R_WRITE, R_READ_BACK, R_MISMATCH, R_RESTORE, R_READ_RESTORED,
    R_MISMATCH_RESTORED, R_NAND_REF, R_ROUNDS, R_SD_INIT_FAIL, R_SD_READ_FAIL,
    R_SD_MISMATCH, R_NAND_FAIL, R_NAND_MISMATCH, R_FINAL_READ, R_FINAL_MISMATCH,
    R_HIGH_SPEED,
};

enum { MODE_CAPTURE = 0, MODE_WRITE = 1, MODE_SWITCH = 2 };

static uint32_t diff(const uint32_t *a, const uint32_t *b, uint32_t words)
{
    uint32_t n = 0;

    for (uint32_t i = 0; i < words; i++)
        n += a[i] != b[i];
    return n;
}

int main(void)
{
    static uint32_t sector[128];
    uint32_t mode = R[R_MODE];
    uint32_t first = 0xFFFFFFFFu;
    const uint8_t *mbr = (const uint8_t *)sector;

    for (uint32_t i = 0; i < 32u; i++)
        if (i != R_MODE)
            R[i] = 0xDEADu;
    R[R_MAGIC] = 0x53444E31u;

    /* sd_release_pins() restores the pin state of this moment, so the NAND
     * pins must be set up first. */
    ((void (*)(void))0x2648u)();
    nand_init();

    R[R_STAGE] = 1;
    R[R_SD_INIT] = (uint32_t)sd_init();
    if (R[R_SD_INIT])
        return 0;
    R[R_HIGH_SPEED] = (uint32_t)sd_high_speed();

    R[R_STAGE] = 2;
    R[R_MBR] = (uint32_t)sd_read_block(0, sector);
    if (R[R_MBR] || mbr[510] != 0x55 || mbr[511] != 0xAA) {
        R[R_MBR] |= 0x100u;
        return 0;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        const uint8_t *e = mbr + 446 + 16 * i;
        uint32_t start = e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24;

        if (e[4] && start < first)
            first = start;
    }
    R[R_FIRST_PART] = first;
    if (first < LBA + BLOCKS)
        return 0;

    R[R_STAGE] = 3;
    R[R_READ_ORIG] = (uint32_t)sd_read_blocks(LBA, BLOCKS, ORIG);
    if (R[R_READ_ORIG] || mode == MODE_CAPTURE)
        return 0;

    if (mode == MODE_WRITE) {
        uint32_t x = 0x12345678u;

        for (uint32_t i = 0; i < WORDS; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            PATTERN[i] = x;
        }
        R[R_STAGE] = 4;
        R[R_WRITE] = (uint32_t)sd_write_blocks(LBA, BLOCKS, PATTERN);
        R[R_READ_BACK] = (uint32_t)sd_read_blocks(LBA, BLOCKS, BACK);
        R[R_MISMATCH] = diff(PATTERN, BACK, WORDS);

        R[R_STAGE] = 5;
        R[R_RESTORE] = (uint32_t)sd_write_blocks(LBA, BLOCKS, ORIG);
        R[R_READ_RESTORED] = (uint32_t)sd_read_blocks(LBA, BLOCKS, BACK);
        R[R_MISMATCH_RESTORED] = diff(ORIG, BACK, WORDS);
        R[R_STAGE] = 6;
        return 0;
    }

    R[R_STAGE] = 7;
    sd_release_pins();
    nand_init();
    R[R_NAND_REF] = (uint32_t)nand_read_page_raw(NAND_PAGE, NREF);
    R[R_SD_INIT_FAIL] = R[R_SD_READ_FAIL] = R[R_SD_MISMATCH] = 0;
    R[R_NAND_FAIL] = R[R_NAND_MISMATCH] = 0;
    for (uint32_t i = 0; i < ROUNDS; i++) {
        if (sd_init()) {
            R[R_SD_INIT_FAIL]++;
        } else if (sd_read_blocks(LBA, 64u, BACK)) {
            R[R_SD_READ_FAIL]++;
        } else if (diff(ORIG, BACK, 64u * 128u)) {
            R[R_SD_MISMATCH]++;
        }
        sd_release_pins();
        nand_init();
        if (nand_read_page_raw(NAND_PAGE, NBACK))
            R[R_NAND_FAIL]++;
        else if (diff(NREF, NBACK, NAND_PAGE_RAW / 4u))
            R[R_NAND_MISMATCH]++;
        R[R_ROUNDS] = i + 1u;
    }

    R[R_STAGE] = 8;
    if (sd_init() == 0) {
        R[R_FINAL_READ] = (uint32_t)sd_read_blocks(LBA, BLOCKS, BACK);
        R[R_FINAL_MISMATCH] = diff(ORIG, BACK, WORDS);
    }
    sd_release_pins();
    R[R_STAGE] = 9;
    return 0;
}
