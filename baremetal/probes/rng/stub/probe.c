#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define HEADER_BASE       0x32008000u
#define OUTPUT_BASE       0x32010000u
#define SAMPLE_WORDS      1000000u
#define STREAM_BYTES      (SAMPLE_WORDS * 4u)

#define NC_BASE           0x48001580u
#define NC_END            0x48002000u
#define NC_WORDS          ((NC_END - NC_BASE) / 4u)
#define DRIVE_ADDR        0x48000400u

#define MAGIC             0x524E4730u
#define VERSION           2u

static void capture_sweep(volatile uint32_t *out)
{
    uint32_t index = 0;

    for (uint32_t i = 0; i < SAMPLE_WORDS; i++) {
        out[i] = REG32(NC_BASE + index * 4u);
        index++;
        if (index == NC_WORDS)
            index = 0;
    }
}

static void capture_fixed(uint32_t address, volatile uint32_t *out)
{
    for (uint32_t i = 0; i < SAMPLE_WORDS; i++)
        out[i] = REG32(address);
}

static void capture_driven(volatile uint32_t *out)
{
    uint32_t original = REG32(DRIVE_ADDR);

    for (uint32_t i = 0; i < SAMPLE_WORDS; i++) {
        uint32_t pattern = (i & 1u) ? 0xFFFFFFFFu : 0u;

        REG32(DRIVE_ADDR) = pattern;
        (void)REG32(DRIVE_ADDR);
        __asm__ volatile ("" : : : "memory");
        out[i] = REG32(NC_BASE);
    }

    REG32(DRIVE_ADDR) = original;
}

void stub_main(void)
{
    volatile uint32_t *header =
        (volatile uint32_t *)(uintptr_t)HEADER_BASE;
    volatile uint32_t *sweep =
        (volatile uint32_t *)(uintptr_t)OUTPUT_BASE;
    volatile uint32_t *fixed1580 =
        (volatile uint32_t *)(uintptr_t)(OUTPUT_BASE + STREAM_BYTES);
    volatile uint32_t *fixed1584 =
        (volatile uint32_t *)(uintptr_t)(OUTPUT_BASE + 2u * STREAM_BYTES);
    volatile uint32_t *driven =
        (volatile uint32_t *)(uintptr_t)(OUTPUT_BASE + 3u * STREAM_BYTES);

    header[0] = MAGIC;
    header[1] = VERSION;
    header[2] = 0xFFFFFFFFu;
    header[3] = SAMPLE_WORDS;
    header[4] = NC_BASE;
    header[5] = NC_END;
    header[6] = NC_WORDS;
    header[7] = DRIVE_ADDR;
    header[8] = (uint32_t)(uintptr_t)sweep;
    header[9] = (uint32_t)(uintptr_t)fixed1580;
    header[10] = (uint32_t)(uintptr_t)fixed1584;
    header[11] = (uint32_t)(uintptr_t)driven;
    header[12] = STREAM_BYTES;

    header[2] = 1u;
    capture_sweep(sweep);
    header[2] = 2u;
    capture_fixed(NC_BASE, fixed1580);
    header[2] = 3u;
    capture_fixed(NC_BASE + 4u, fixed1584);
    header[2] = 4u;
    capture_driven(driven);
    header[2] = 0u;
}
