#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SYSCTRL(off) REG32(0x08000000u + (off))
#define MCI(off) REG32(0x20020000u + (off))
#define L2(off) REG32(0x2002C000u + (off))
#define OUT ((volatile uint32_t *)(uintptr_t)0x48001100u)

void stub_main(void)
{
    static const uint8_t mci_offsets[] = {
        0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20,
        0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40,
    };

    for (uint32_t i = 0; i < 64; i++)
        OUT[i] = 0;

    OUT[0] = 0x53445052u;
    OUT[1] = 1;
    OUT[2] = 1;
    OUT[3] = 0;
    OUT[4] = 32;

    OUT[8] = SYSCTRL(0x0C);
    OUT[9] = SYSCTRL(0x10);
    OUT[10] = SYSCTRL(0x74);
    OUT[11] = SYSCTRL(0x78);
    OUT[12] = SYSCTRL(0x9C);
    OUT[13] = SYSCTRL(0xA0);
    OUT[14] = SYSCTRL(0xA4);
    OUT[15] = SYSCTRL(0xBC);
    OUT[16] = SYSCTRL(0xC0);
    OUT[17] = SYSCTRL(0xD4);

    for (uint32_t i = 0; i < sizeof(mci_offsets); i++)
        OUT[18 + i] = MCI(mci_offsets[i]);

    OUT[35] = L2(0x80);
    OUT[36] = L2(0x84);
    OUT[37] = L2(0x88);
    OUT[38] = L2(0x90);
    OUT[39] = L2(0xA0);
}
