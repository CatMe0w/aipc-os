#include "dm9000_bus.h"

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static const uint8_t cases[] = {
    DM9K_VIDL,  /* 0x28 -> 0x46 */
    DM9K_VIDH,  /* 0x29 -> 0x0A */
    DM9K_PIDL,  /* 0x2A -> 0x00 */
    DM9K_PIDH,  /* 0x2B -> 0x90 */
    DM9K_CHIPR, /* 0x2C -> 0x19 */
};

#define NCASES (sizeof(cases) / sizeof(cases[0]))

void stub_main(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;

    OUT[0] = 0x444D3949u; /* "DM9I" */

    dm9k_bus_init();

    dm9k_data_in();
    OUT[2] = dm9k_data_sample();
    dm9k_delay(5);
    OUT[3] = dm9k_data_sample();

    OUT[4] = dm9k_read(DM9K_VIDL);
    OUT[5] = dm9k_read(DM9K_VIDH);
    OUT[6] = dm9k_read(DM9K_PIDL);
    OUT[7] = dm9k_read(DM9K_PIDH);
    OUT[8] = dm9k_read(DM9K_CHIPR);

    for (uint32_t n = 0; n < NCASES; n++) {
        volatile uint32_t *p = OUT + 12u + n * 4u;
        uint8_t reg = cases[n];

        dm9k_index_write(reg);
        p[0] = reg;
        p[1] = dm9k_data_read();
        p[2] = dm9k_index_read();
        dm9k_delay(5);
        p[3] = dm9k_index_read();
    }

    dm9k_index_write(DM9K_VIDL);
    (void)dm9k_data_read();
    dm9k_index_write(DM9K_CHIPR);
    (void)dm9k_data_read();
    OUT[36] = dm9k_index_read();

    dm9k_snapshot(OUT + 44);

    OUT[1] = 1u;
}
