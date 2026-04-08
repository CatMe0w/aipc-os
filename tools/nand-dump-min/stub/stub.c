/*
 * AK7802 host-driven NAND dump stub
 *
 * Called via EXECUTE, does one operation, returns to bootrom.
 * The host controls everything: it writes parameters to a fixed
 * SRAM address before each EXECUTE, then reads back results via
 * the bootrom's UPLOAD command after the stub returns.
 *
 * All NAND operations use bootrom ROM helper functions directly.
 * No USB TX code in the stub, avoiding L2BUF_00/DMA conflicts.
 *
 * Parameter block at 0x48000040 (written by host via DOWNLOAD):
 *
 *   +0x00  command      (uint32)  0 = hw_init, 1 = probe+read
 *   +0x04  probe_param  (5 x uint32 = 20 bytes)
 *   +0x18  page         (uint32)  page number for read
 *   +0x1C  chunks       (uint32)  chunks per page (each 512 B)
 *   +0x20  chunk_size   (uint32)  bytes per chunk (8 for ID, 512 for page)
 *   +0x24  timing0      (uint32)  NF timing reg 0 (0 = don't change)
 *   +0x28  timing1      (uint32)  NF timing reg 1 (0 = don't change)
 *   +0x2C  pre_delay    (uint32)  ticks to delay before probe (0 = skip)
 *   +0x30  status       (uint32)  output: written by stub (0 = ok)
 *
 * Data output at 0x48000400 (read by host via UPLOAD):
 *   Up to chunks * chunk_size bytes.
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* L2 buffer assignment register */
#define L2CTR_ASSIGN_REG1   REG32(0x2002C090)

/* Parameter block in L2 buffer SRAM */
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
    uint32_t status;
};

/* Bootrom helper entry points */
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
        /* Command 0: hardware init only */
        ROM_NF_BOOT_HW_INIT();
        p->status = 0;
        return;
    }

    /*
     * Command 1: hw_init + timings + probe + read.
     *
     * Write progress markers to status so the host can identify
     * where the stub hangs if it never returns.
     *   0x10 = entering command 1
     *   0x11 = hw_init done
     *   0x12 = timings done
     *   0x13 = probe sequence done
     *   0x14 = starting chunk reads
     *   0x15+c = chunk c done
     *   0    = all done (success)
     *   2    = read_chunk_to_buf returned 0
     */
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

    uint32_t *out = (uint32_t *)DATA_BASE;
    uint32_t chunk_size = p->chunk_size;
    uint32_t chunks = p->chunks;
    p->status = 0x14;

    for (uint32_t c = 0; c < chunks; c++) {
        if (!ROM_NF_READ_CHUNK_TO_BUF(out, chunk_size)) {
            p->status = 2;
            L2CTR_ASSIGN_REG1 = saved_assign;
            return;
        }
        out += chunk_size / 4;
        p->status = 0x15 + c;
    }

    L2CTR_ASSIGN_REG1 = saved_assign;
    p->status = 0;
}
