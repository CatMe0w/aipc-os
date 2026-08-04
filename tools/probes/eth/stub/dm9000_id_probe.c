#include "dm9000_bus.h"

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void read_ids(volatile uint32_t *p)
{
    p[0] = dm9k_read(DM9K_VIDL);
    p[1] = dm9k_read(DM9K_VIDH);
    p[2] = dm9k_read(DM9K_PIDL);
    p[3] = dm9k_read(DM9K_PIDH);
    p[4] = dm9k_read(DM9K_NCR);
    p[5] = dm9k_read(DM9K_NSR);
    p[6] = dm9k_read(DM9K_CHIPR);
}

void stub_main(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;

    OUT[0] = 0x444D3941u; /* "DM9A" */

    dm9k_snapshot(OUT + 4);
    dm9k_bus_init();
    dm9k_snapshot(OUT + 12);

    read_ids(OUT + 20);

    dm9k_write(DM9K_NCR, 0x01u);
    dm9k_delay(20);
    dm9k_write(DM9K_NCR, 0x00u);
    dm9k_delay(20);

    read_ids(OUT + 32);
    dm9k_snapshot(OUT + 44);

    OUT[1] = 1u;
}
