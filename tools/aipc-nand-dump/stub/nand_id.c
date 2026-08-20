/*
 * AIPC NAND ID & timing stub
 *
 * Initializes the NAND controller, reads the 8-byte NAND ID, then
 * probes the NAND header (page 0) to extract factory timing values.
 * Replicates the bootrom's probe_flash_boot_source logic: tries all
 * 8 probe configurations until the "ANYKA382" signature is found.
 *
 * Result block (at RESULT_BASE = 0x48000040):
 *   +0x00  status       (u32)  0 = OK, 1 = ID read failed
 *   +0x04  id_word0     (u32)  raw ID bytes 0-3
 *   +0x08  id_word1     (u32)  raw ID bytes 4-7
 *   +0x0C  timing_cfg0  (u32)  NFC timing register 0  (0 if header not found)
 *   +0x10  timing_cfg1  (u32)  NFC timing register 1  (0 if header not found)
 *   +0x14  hdr_delay    (u32)  delay_pair from header (0 if header not found)
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define RESULT_BASE          0x48000040u
#define SCRATCH_BUF          0x48000C00u
#define L2CTR_ASSIGN_REG1    REG32(0x2002C090u)

/* bootrom helpers */
#define ROM_NF_BOOT_HW_INIT         ((void (*)(void))0x00002648u)
#define ROM_NF_ISSUE_PROBE_SEQUENCE ((int (*)(const void *, uint32_t))0x0000293Cu)
#define ROM_NF_READ_CHUNK_TO_BUF    ((int (*)(void *, uint32_t))0x00002C3Cu)

struct nf_probe_param {
    uint32_t counts;
    uint32_t command;
    uint32_t timing_cfg0;
    uint32_t timing_cfg1;
    uint32_t delay_pair;
};

struct nand_id_result {
    uint32_t status;
    uint32_t id_word0;
    uint32_t id_word1;
    uint32_t timing_cfg0;
    uint32_t timing_cfg1;
    uint32_t hdr_delay;
};

/* NAND header signature "ANYKA382" as little-endian words */
#define SIG_WORD0  0x4B594E41u  /* "ANYK" */
#define SIG_WORD1  0x32383341u  /* "A382" */

/*
 * Bootrom probe configurations (matches ROM table at 0x4758).
 * Only counts and command vary; timing/delay are fixed.
 */
static const uint32_t probe_configs[8][2] = {
    { 0x01010101u, 0x00000004u },  /* 1-cmd, 4 addr */
    { 0x01020101u, 0x00300004u },  /* 2-cmd, 4 addr */
    { 0x01010101u, 0x00000003u },  /* 1-cmd, 3 addr */
    { 0x01020101u, 0x00300003u },  /* 2-cmd, 3 addr */
    { 0x01010101u, 0x00000002u },  /* 1-cmd, 2 addr */
    { 0x01020101u, 0x00300002u },  /* 2-cmd, 2 addr */
    { 0x01010101u, 0x00000005u },  /* 1-cmd, 5 addr */
    { 0x01020101u, 0x00300005u },  /* 2-cmd, 5 addr */
};

void stub_main(void)
{
    uint32_t *tmp = (uint32_t *)SCRATCH_BUF;
    uint32_t saved = L2CTR_ASSIGN_REG1;

    /* Configure L2 buffer for NAND and initialize controller */
    L2CTR_ASSIGN_REG1 = saved & 0xFFFFFFC0u;
    ROM_NF_BOOT_HW_INIT();

    /* Read NAND ID */
    const struct nf_probe_param id_param = {
        .counts    = (1u << 16),          /* cmd_count = 1 */
        .command   = 1u | (0x90u << 8),   /* addr_bytes = 1, cmd1 = 0x90 */
        .timing_cfg0 = 0,
        .timing_cfg1 = 0,
        .delay_pair  = (10u << 16),       /* seq wait = 10 ticks */
    };

    tmp[0] = 0;
    tmp[1] = 0;
    (void)ROM_NF_ISSUE_PROBE_SEQUENCE(&id_param, 0);

    if (!ROM_NF_READ_CHUNK_TO_BUF(tmp, 8)) {
        L2CTR_ASSIGN_REG1 = saved;
        volatile struct nand_id_result *r =
            (volatile struct nand_id_result *)RESULT_BASE;
        r->status = 1;
        return;
    }

    /* Save ID before further DMA trashes SCRATCH_BUF area */
    uint32_t id0 = tmp[0];
    uint32_t id1 = tmp[1];

    /* Probe NAND header for factory timing */
    uint32_t timing0 = 0, timing1 = 0, hdr_delay = 0;

    for (int i = 0; i < 8; i++) {
        struct nf_probe_param pp = {
            .counts      = probe_configs[i][0],
            .command     = probe_configs[i][1],
            .timing_cfg0 = 0,
            .timing_cfg1 = 0,
            .delay_pair  = 0x000A000Au,   /* same as bootrom probe */
        };

        (void)ROM_NF_ISSUE_PROBE_SEQUENCE(&pp, 0);

        /* Read first 32 bytes of page 0 into scratch */
        if (!ROM_NF_READ_CHUNK_TO_BUF(tmp, 32))
            continue;

        /* Check "ANYKA382" signature at offset +0x04 */
        if (tmp[1] == SIG_WORD0 && tmp[2] == SIG_WORD1) {
            /*
             * NAND header layout (from bootrom reverse-engineering):
             *   +0x00  prefix
             *   +0x04  signature "ANYKA382"
             *   +0x0C  load_desc.counts
             *   +0x10  load_desc.command
             *   +0x14  load_desc.timing_cfg0 -> NFC register 0x2002A05C
             *   +0x18  load_desc.timing_cfg1 -> NFC register 0x2002A060
             *   +0x1C  load_desc.delay_pair
             */
            timing0   = tmp[5];   /* offset +0x14 */
            timing1   = tmp[6];   /* offset +0x18 */
            hdr_delay = tmp[7];   /* offset +0x1C */
            break;
        }
    }

    L2CTR_ASSIGN_REG1 = saved;

    /* Write results */
    volatile struct nand_id_result *r =
        (volatile struct nand_id_result *)RESULT_BASE;
    r->status      = 0;
    r->id_word0    = id0;
    r->id_word1    = id1;
    r->timing_cfg0 = timing0;
    r->timing_cfg1 = timing1;
    r->hdr_delay   = hdr_delay;
}
