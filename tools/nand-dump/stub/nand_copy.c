/*
 * AIPC NAND copy stub
 *
 * Reads a batch of NAND pages (including OOB/ECC data) into DDR, then
 * returns to the bootrom.  The host retrieves the data via read_mem.
 *
 * Physical page layout (continuous-stream mode):
 *   For a 2KB-page NAND: [512B data + 16B spare] x 4 = 2112 bytes/page
 *   The stub reads the raw physical stream: 4 x 512B + 1 x 64B = 2112B.
 *
 * On read error, the affected page is filled with 0xFF (erased pattern)
 * and the error counter is incremented.  The stub continues with the
 * remaining pages.
 *
 * Parameter block (at PARAM_BASE = 0x48000040, written by host):
 *   +0x00  start_page       (u32)
 *   +0x04  batch_pages      (u32)
 *   +0x08  ddr_dest         (u32)  DDR base address for this batch
 *   +0x0C  chunks_per_page  (u32)  512B data chunks per page (1/4/8)
 *   +0x10  probe_counts     (u32)  nf_probe_param.counts
 *   +0x14  probe_command    (u32)  nf_probe_param.command
 *   +0x18  probe_timing0    (u32)  nf_probe_param.timing_cfg0
 *   +0x1C  probe_timing1    (u32)  nf_probe_param.timing_cfg1
 *   +0x20  probe_delay      (u32)  nf_probe_param.delay_pair
 *   +0x24  nfc_timing0      (u32)  NFC timing register 0 (0 = keep default)
 *   +0x28  nfc_timing1      (u32)  NFC timing register 1 (0 = keep default)
 *
 * Result block (at PARAM_BASE, written by stub before return):
 *   +0x00  status       (u32)  0 = OK, 1 = completed with errors
 *   +0x04  pages_done   (u32)  number of pages read
 *   +0x08  error_count  (u32)  number of pages that had read errors
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define PARAM_BASE           0x48000040u
#define L2CTR_ASSIGN_REG1    REG32(0x2002C090u)
#define NFC_TIMING_REG0      REG32(0x2002A05Cu)
#define NFC_TIMING_REG1      REG32(0x2002A060u)

/* bootrom helpers */
#define ROM_NF_BOOT_HW_INIT         ((void (*)(void))0x00002648u)
#define ROM_NF_ISSUE_PROBE_SEQUENCE ((int (*)(const void *, uint32_t))0x0000293Cu)
#define ROM_NF_READ_CHUNK_TO_BUF    ((int (*)(void *, uint32_t))0x00002C3Cu)

struct nand_copy_params {
    uint32_t start_page;
    uint32_t batch_pages;
    uint32_t ddr_dest;
    uint32_t chunks_per_page;
    uint32_t probe_counts;
    uint32_t probe_command;
    uint32_t probe_timing0;
    uint32_t probe_timing1;
    uint32_t probe_delay;
    uint32_t nfc_timing0;
    uint32_t nfc_timing1;
};

struct nand_copy_result {
    uint32_t status;
    uint32_t pages_done;
    uint32_t error_count;
};

struct nf_probe_param {
    uint32_t counts;
    uint32_t command;
    uint32_t timing_cfg0;
    uint32_t timing_cfg1;
    uint32_t delay_pair;
};

static void fill_ff(void *dst, uint32_t size)
{
    uint32_t *p = (uint32_t *)dst;
    uint32_t words = size >> 2;
    while (words--)
        *p++ = 0xFFFFFFFFu;
}

void stub_main(void)
{
    volatile struct nand_copy_params *p =
        (volatile struct nand_copy_params *)PARAM_BASE;

    /* Snapshot all parameters into locals before NAND DMA trashes the
     * L2 SRAM region at 0x48000000-0x480001FF */
    uint32_t start_page      = p->start_page;
    uint32_t batch_pages     = p->batch_pages;
    uint32_t ddr_dest        = p->ddr_dest;
    uint32_t chunks_per_page = p->chunks_per_page;

    struct nf_probe_param read_param;
    read_param.counts     = p->probe_counts;
    read_param.command    = p->probe_command;
    read_param.timing_cfg0 = p->probe_timing0;
    read_param.timing_cfg1 = p->probe_timing1;
    read_param.delay_pair = p->probe_delay;

    uint32_t nfc_t0 = p->nfc_timing0;
    uint32_t nfc_t1 = p->nfc_timing1;

    uint32_t oob_size      = chunks_per_page << 4;   /* chunks * 16 */
    uint32_t raw_page_size = chunks_per_page * 528u;
    uint32_t pages_done    = 0;
    uint32_t error_count   = 0;

    uint32_t saved = L2CTR_ASSIGN_REG1;
    L2CTR_ASSIGN_REG1 = saved & 0xFFFFFFC0u;
    ROM_NF_BOOT_HW_INIT();

    /* Apply factory NFC timing from NAND header (overrides conservative
     * defaults set by ROM_NF_BOOT_HW_INIT) */
    if (nfc_t0)
        NFC_TIMING_REG0 = nfc_t0;
    if (nfc_t1)
        NFC_TIMING_REG1 = nfc_t1;

    for (uint32_t i = 0; i < batch_pages; i++) {
        uint32_t page = start_page + i;
        uint8_t *page_base = (uint8_t *)(uintptr_t)(ddr_dest + i * raw_page_size);
        uint8_t *dst = page_base;
        int failed = 0;

        (void)ROM_NF_ISSUE_PROBE_SEQUENCE(&read_param, page);

        /* Read data chunks (512 bytes each) */
        for (uint32_t c = 0; c < chunks_per_page && !failed; c++) {
            if (!ROM_NF_READ_CHUNK_TO_BUF(dst, 512))
                failed = 1;
            else
                dst += 512;
        }

        /* Read OOB/spare (chunks_per_page * 16 bytes) */
        if (!failed) {
            if (!ROM_NF_READ_CHUNK_TO_BUF(dst, oob_size))
                failed = 1;
        }

        if (failed) {
            fill_ff(page_base, raw_page_size);
            error_count++;
        }

        pages_done++;
    }

    L2CTR_ASSIGN_REG1 = saved;

    /* Write results back to PARAM_BASE */
    volatile struct nand_copy_result *r =
        (volatile struct nand_copy_result *)PARAM_BASE;
    r->status      = (error_count > 0) ? 1 : 0;
    r->pages_done  = pages_done;
    r->error_count = error_count;
}
