/* One operation for each EXECUTE. The host writes the parameters before the
 * call and reads DATA_BASE after it. For a page read, DATA_BASE holds the
 * 512-byte chunks first and then 16 spare bytes for each chunk. */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define L2CTR_ASSIGN_REG1   REG32(0x2002C090)

#define PARAM_BASE  0x48000040
#define DATA_BASE   0x48000400

struct stub_params {
    uint32_t command;
    uint32_t probe_param[5];
    uint32_t page;
    uint32_t chunks;
    uint32_t chunk_size;
    uint32_t timing0;
    uint32_t timing1;
    uint32_t pre_delay;
    uint32_t status;     /* 0 = ok. see the markers in stub_main */
};

#define ROM_NF_BOOT_HW_INIT         ((void (*)(void))0x2648u)
#define ROM_NF_SET_BOOT_TIMINGS     ((int (*)(uint32_t, uint32_t))0x277Cu)
#define ROM_NF_DELAY_TICKS          ((int (*)(uint16_t))0x27D8u)
#define ROM_NF_ISSUE_PROBE_SEQUENCE ((int (*)(const void *, uint32_t))0x293Cu)
#define ROM_NF_READ_CHUNK_TO_BUF    ((int (*)(void *, uint32_t))0x2C3Cu)

void stub_main(void)
{
    volatile struct stub_params *p = (volatile struct stub_params *)PARAM_BASE;
    uint32_t cmd = p->command;

    if (cmd == 0) {
        ROM_NF_BOOT_HW_INIT();
        p->status = 0;
        return;
    }

    /* The status word shows where the stub stopped if it never returns:
     * 0x10 entered, 0x11 hw_init done, 0x12 timings done, 0x13 command
     * sequence done, 0x14 chunk reads started, 0x15 + c chunk c done.
     * 2 and 3 mean that a data read or the spare read failed. */
    p->status = 0x10;

    uint32_t saved_assign = L2CTR_ASSIGN_REG1;
    L2CTR_ASSIGN_REG1 &= 0xFFFFFFC0u;

    ROM_NF_BOOT_HW_INIT();
    p->status = 0x11;

    if (p->timing0 || p->timing1)
        ROM_NF_SET_BOOT_TIMINGS(p->timing0, p->timing1);
    if (p->pre_delay)
        ROM_NF_DELAY_TICKS((uint16_t)p->pre_delay);
    p->status = 0x12;

    ROM_NF_ISSUE_PROBE_SEQUENCE((const void *)&p->probe_param[0], p->page);
    p->status = 0x13;

    uint8_t *out = (uint8_t *)DATA_BASE;
    uint32_t chunk_size = p->chunk_size;
    uint32_t chunks = p->chunks;
    p->status = 0x14;

    for (uint32_t c = 0; c < chunks; c++) {
        if (!ROM_NF_READ_CHUNK_TO_BUF(out, chunk_size)) {
            p->status = 2;
            L2CTR_ASSIGN_REG1 = saved_assign;
            return;
        }
        out += chunk_size;
        p->status = 0x15 + c;
    }

    if (chunk_size == 512 && chunks) {
        uint32_t spare_size = chunks << 4;
        if (!ROM_NF_READ_CHUNK_TO_BUF(out, spare_size)) {
            p->status = 3;
            L2CTR_ASSIGN_REG1 = saved_assign;
            return;
        }
    }

    L2CTR_ASSIGN_REG1 = saved_assign;
    p->status = 0;
}
