#include "lcd_probe_common.h"

#define SRC_BASE        0x30100000u
#define SRC_W           320u
#define SRC_H           200u
#define SCALE           2u
#define OUT_W           (SRC_W * SCALE)
#define OUT_H           (SRC_H * SCALE)
#define X_OFFSET        ((FB_WIDTH - OUT_W) / 2u)
#define Y_OFFSET        ((FB_HEIGHT - OUT_H) / 2u)

static uint16_t rgb565_from_argb(uint32_t argb)
{
    uint32_t r = (argb >> 16) & 0xFFu;
    uint32_t g = (argb >> 8) & 0xFFu;
    uint32_t b = argb & 0xFFu;

    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void fill_source_pattern(void)
{
    volatile uint32_t *src = (volatile uint32_t *)(uintptr_t)SRC_BASE;
    uint32_t y;
    uint32_t x;

    for (y = 0; y < SRC_H; ++y) {
        for (x = 0; x < SRC_W; ++x) {
            uint32_t r = (x * 255u) / (SRC_W - 1u);
            uint32_t g = (y * 255u) / (SRC_H - 1u);
            uint32_t b = ((x ^ y) & 1u) ? 0x30u : 0xC0u;
            src[y * SRC_W + x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void blit_source_to_framebuffer(void)
{
    const uint32_t *src = (const uint32_t *)(uintptr_t)SRC_BASE;
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)FB_CPU_BASE;
    uint32_t y;
    uint32_t x;

    for (y = 0; y < SRC_H; ++y) {
        const uint32_t *src_row = src + y * SRC_W;
        volatile uint16_t *dst_row0 =
            fb + (Y_OFFSET + y * SCALE) * FB_WIDTH + X_OFFSET;
        volatile uint16_t *dst_row1 = dst_row0 + FB_WIDTH;

        for (x = 0; x < SRC_W; ++x) {
            uint16_t pixel = rgb565_from_argb(src_row[x]);
            uint32_t dst_x = x * SCALE;

            dst_row0[dst_x] = pixel;
            dst_row0[dst_x + 1u] = pixel;
            dst_row1[dst_x] = pixel;
            dst_row1[dst_x + 1u] = pixel;
        }
    }
}

void stub_main(void)
{
    lcd_init_from_eboot();
    build_l1_table(SECTION_RW_CACHED);
    enable_mmu(1);

    fill_framebuffer(FB_CPU_BASE, 0x0000u);
    fill_source_pattern();
    blit_source_to_framebuffer();
    drain_write_buffer();

    record_common(0x4C44424Cu, 0xB117u);
    out[10] = SRC_BASE;
    out[11] = l1_entry(SRC_BASE);
    out[12] = ((volatile uint16_t *)(uintptr_t)FB_CPU_BASE)[Y_OFFSET * FB_WIDTH + X_OFFSET];
    out[13] = ((volatile uint16_t *)(uintptr_t)FB_CPU_BASE)[(Y_OFFSET + OUT_H / 2u) * FB_WIDTH + X_OFFSET + OUT_W / 2u];
    out[14] = ((volatile uint32_t *)(uintptr_t)SRC_BASE)[0];
    out[15] = ((volatile uint32_t *)(uintptr_t)SRC_BASE)[SRC_H * SRC_W - 1u];

    disable_mmu_dcache();
}
