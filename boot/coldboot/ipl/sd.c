#include <stdint.h>
#include "sd.h"

#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define SYSCTRL(off)  REG32(0x08000000u + (off))
#define MCI(off)      REG32(0x20020000u + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_RESP0     MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALEN   MCI(0x28)
#define MCI_DATACTRL  MCI(0x2C)
#define MCI_STA       MCI(0x34)
#define MCI_MASK      MCI(0x38)
#define MCI_DMACTRL   MCI(0x3C)

#define MCI_ENABLE        (1u << 20)
#define MCI_FAIL          (1u << 19)
#define MCI_CLK_EN        (1u << 16)
#define MCI_PWRSAVE       (1u << 17)

#define CPSM_ENABLE       (1u << 0)
#define CPSM_RESPONSE     (1u << 7)
#define CPSM_LONGRSP      (1u << 8)
#define CPSM_RSPCRC_NOCHK (1u << 10)

#define DPSM_ENABLE       (1u << 0)
#define DPSM_DIR_READ     (1u << 1)
#define DPSM_BUS_4BIT     (1u << 3)   /* DATACTRL busmode field = 4-bit */
#define DPSM_BLKSZ_512    (512u << 16)

#define STA_RESPCRCFAIL   (1u << 0)
#define STA_RESPTIMEO     (1u << 2)
#define STA_RESPEND       (1u << 4)
#define STA_CMDSENT       (1u << 5)

#define RESP_MASK         (STA_RESPEND | STA_RESPTIMEO | STA_RESPCRCFAIL)

/* L2 SRAM controller registers, the same block nand.c routes NF DMA
 * through. Block reads are DMAed into an L2 buffer, then bulk-copied to
 * RAM, avoiding thousands of slow cross-clock-domain MCI FIFO accesses. */
#define L2_CONBUF0_7      REG32(0x2002C088u)  /* per-buffer enable/clear */
#define L2_BUFASSIGN1     REG32(0x2002C090u)  /* device -> buffer-id map */
#define L2_BUFSTAT1       REG32(0x2002C0A0u)  /* per-buffer 64B chunk count */

/* MCI uses L2 common buffer 4; NFC (nand.c) uses buffer 5, so the SD and
 * NAND fallback paths never contend for the same slot. */
#define SD_L2_BUF_ID      4u
#define SD_L2_BUF_ADDR    (0x48000000u + SD_L2_BUF_ID * 512u)

/* MCI_DMACTRL value routing the data phase through the L2 buffer:
 * BUFEN (bit 0) + SIZE field = 128 words (512 bytes) at bit 17. */
#define MCI_DMACTRL_L2    0x01000001u

static int      g_sdhc;
static int      g_bus_4bit;  /* set once ACMD6 puts the card in 4-bit mode */
static uint32_t g_rca_arg;   /* RCA already shifted into CMD7 arg form */

/* Baseline SYSCTRL values captured at sd_init() entry so sd_release_pins()
 * can hand pad control back to the NAND path on fallback. */
static struct {
    uint32_t sc_0c, sc_74, sc_78, sc_a0;
    int valid;
} g_pin_save;

static void small_delay(void)
{
    for (volatile uint32_t i = 0; i < 2000u; i++)
        __asm__ volatile ("" : : : "memory");
}

#define WAIT_LIMIT 500000u
static uint32_t wait_sta(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t sta = MCI_STA;
        if (sta & mask)
            return sta;
    }
    return 0;
}

static void sd_stop_transfer(void)
{
    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DATALEN  = 0;
    MCI_DMACTRL  = 0;
    MCI_MASK     = 0;
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));
}

static void sd_quiesce(void)
{
    sd_stop_transfer();
    L2_BUFASSIGN1 &= ~(0x7u << 12);
}

static int send_resp(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                     uint32_t *resp_out)
{
    if (MCI_CMD & CPSM_ENABLE) {
        MCI_CMD = 0;
        small_delay();
    }
    MCI_ARG = arg;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | flags | (cmd_idx << 1);

    uint32_t sta = wait_sta(RESP_MASK);
    /* CRCFAIL counts as a response. Caller wants RESP0 regardless. */
    if (!(sta & (STA_RESPEND | STA_RESPCRCFAIL)))
        return -1;
    if (resp_out)
        *resp_out = MCI_RESP0;
    return 0;
}

/* Program the MCI bus-clock divider. Mirrors SDHC_SetClock @0x82709b44 in
 * sdhc_anyka.dll: writing the divider field implicitly clears CLK_EN, so
 * we capture its prior state and restore it after. PWRSAVE is dropped,
 * since AK98 linux never sets it. Divider formula from sdhc-driver.md:
 * bus = source / (divider + 2), split as low = div - div/2 in [7:0],
 * high = div/2 in [15:8]. */
static void sd_set_clock_divider(uint32_t divider)
{
    uint32_t cur = MCI_CLOCK;
    int was_clk_en = (cur & MCI_CLK_EN) != 0;
    uint32_t low = divider - (divider >> 1);
    uint32_t high = divider >> 1;
    MCI_CLOCK = (cur & 0xFFFF0000u & ~MCI_PWRSAVE) | (high << 8) | low;
    if (was_clk_en)
        MCI_CLOCK |= MCI_CLK_EN;
    small_delay();
}

/* Bind L2 common buffer SD_L2_BUF_ID to the MMC/SD device so MCI block
 * reads DMA into it. Mirrors ak98_l2_alloc's enable sequence: buffer
 * DMA-valid + buffer-valid, device->buffer assignment, status clear. */
static void sd_bind_l2(void)
{
    L2_CONBUF0_7 |= (1u << SD_L2_BUF_ID) | (1u << (SD_L2_BUF_ID + 16));
    /* BUFASSIGN1 holds 3 bits per device; MMC/SD is device index 4. */
    L2_BUFASSIGN1 = (L2_BUFASSIGN1 & ~(0x7u << 12)) | (SD_L2_BUF_ID << 12);
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));
}

int sd_init(void)
{
    g_sdhc = 0;
    g_bus_4bit = 0;
    g_rca_arg = 0;

    g_pin_save.sc_0c = SYSCTRL(0x0C);
    g_pin_save.sc_74 = SYSCTRL(0x74);
    g_pin_save.sc_78 = SYSCTRL(0x78);
    g_pin_save.sc_a0 = SYSCTRL(0xA0);
    g_pin_save.valid = 1;

    /* Clock-gate toggle (SYSCTRL+0x0C bit2). Empirically required to clear
     * sticky state from a prior init attempt. */
    SYSCTRL(0x0C) |=  (1u << 2);
    small_delay();
    SYSCTRL(0x0C) &= ~(1u << 2);
    small_delay();

    /* Sharepin: MDAT2 bit + DATA[7:0] group + GRP3=MMC */
    SYSCTRL(0x78) |= (1u << 29);
    SYSCTRL(0x78) = (SYSCTRL(0x78) & ~(7u << 16)) | (7u << 16);
    SYSCTRL(0x74) = (SYSCTRL(0x74) & ~(3u << 3)) | (2u << 3);
    small_delay();

    /* Pull-up on SD DATA/CMD lines (SYSCTRL+0xA0 bits 7,8). */
    SYSCTRL(0xA0) |= 0x180u;
    small_delay();

    /* Two-step clock enable: assert ENABLE|FAIL first, then add CLK_EN,
     * power-save, and divider. Divider 0xF0 gives a slow-init clock. */
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL;
    small_delay();
    MCI_CLOCK = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | MCI_PWRSAVE | 0xF0u;
    small_delay();

    /* 74+ clock cycles before first command, per SD spec. */
    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    MCI_CMD       = 0;
    MCI_DATACTRL  = 0;
    MCI_DMACTRL   = 0;
    MCI_DATATIMER = 0x00030000u;
    MCI_MASK      = 0;

    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    /* CMD0: GO_IDLE_STATE. No response expected. */
    MCI_ARG = 0;
    MCI_CMD = CPSM_ENABLE;
    wait_sta(STA_CMDSENT | STA_RESPTIMEO);
    small_delay();

    /* CMD8: SEND_IF_COND. Echoes 0x1AA when card supports 2.7-3.6V. */
    uint32_t resp8 = 0;
    int ok8 = 0;
    for (uint32_t r = 0; r < 10u; r++) {
        if (r) small_delay();
        if (send_resp(8, 0x1AAu, CPSM_RESPONSE, &resp8) == 0 && resp8 == 0x1AAu) {
            ok8 = 1;
            break;
        }
    }
    if (!ok8)
        return -8;

    /* ACMD41 OCR loop. HCS=1 in arg announces host supports SDHC. */
    uint32_t ocr_arg = 0x40FF8000u;
    uint32_t ocr = 0;
    int ready = 0;
    for (uint32_t a = 0; a < 100u; a++) {
        uint32_t tmp;
        if (send_resp(55, 0, CPSM_RESPONSE, &tmp) < 0)
            break;
        if (send_resp(41, ocr_arg, CPSM_RESPONSE | CPSM_RSPCRC_NOCHK, &ocr) < 0)
            break;
        if (ocr & 0x80000000u) { ready = 1; break; }
        /* After first reply, narrow voltage window to card's reported range. */
        if (a == 0 && (ocr & 0x00FFFFFFu))
            ocr_arg = (ocr & 0x40FF8000u) | 0x40FF8000u;
        small_delay();
    }
    if (!ready)
        return -41;
    g_sdhc = (ocr & 0x40000000u) ? 1 : 0;

    /* CMD2: ALL_SEND_CID, long response. We don't keep the CID. */
    MCI_ARG = 0;
    MCI_MASK = 0x1FFu;
    MCI_CMD = CPSM_ENABLE | CPSM_RESPONSE | CPSM_LONGRSP | (2u << 1);
    if (!(wait_sta(RESP_MASK) & STA_RESPEND))
        return -2;

    /* CMD3: SEND_RELATIVE_ADDR. R6 = [RCA:16][status:16]. */
    uint32_t r6;
    if (send_resp(3, 0, CPSM_RESPONSE, &r6) < 0)
        return -3;
    g_rca_arg = r6 & 0xFFFF0000u;

    /* CMD7: SELECT_CARD with RCA. Moves card to transfer state. */
    if (send_resp(7, g_rca_arg, CPSM_RESPONSE, 0) < 0)
        return -7;

    small_delay();

    /* ACMD6: switch the card to 4-bit bus. CMD55 (APP_CMD) marks the next
     * command application-specific, then CMD6 arg 2 selects 4-bit. If
     * either step fails the card stays 1-bit; g_bus_4bit keeps the data
     * path consistent so reads still work, just slower. */
    {
        uint32_t tmp;
        if (send_resp(55, g_rca_arg, CPSM_RESPONSE, &tmp) >= 0 &&
            send_resp(6, 0x2u, CPSM_RESPONSE, &tmp) >= 0)
            g_bus_4bit = 1;
    }

    /* Fast transfer clock. bench confirmed divider drives the bus clock
     * linearly; 0x04 (source/6) is the smallest value verified to read
     * cleanly and stays inside the SD default-speed 25 MHz ceiling. */
    sd_set_clock_divider(0x04u);
    sd_bind_l2();
    return 0;
}

void sd_release_pins(void)
{
    sd_quiesce();

    if (!g_pin_save.valid)
        return;
    SYSCTRL(0x0C) = g_pin_save.sc_0c;
    SYSCTRL(0x74) = g_pin_save.sc_74;
    SYSCTRL(0x78) = g_pin_save.sc_78;
    SYSCTRL(0xA0) = g_pin_save.sc_a0;
    g_pin_save.valid = 0;
}

int sd_read_block(uint32_t lba, void *dst)
{
    uint32_t arg = g_sdhc ? lba : (lba << 9);

    /* Tear down any lingering CPSM/DPSM state from the previous call so
     * sticky STA bits don't alias as fresh RESPEND events. The device
     * register writes that follow are themselves slow enough on the AHB
     * to give the controller the settle time a delay would have. */
    MCI_CMD      = 0;
    MCI_DATACTRL = 0;
    MCI_DMACTRL  = 0;

    /* Clear stale L2 buffer status before arming a fresh transfer. */
    L2_CONBUF0_7 |= (1u << (SD_L2_BUF_ID + 24));

    /* Route the data phase through the L2 buffer, then arm DPSM. The
     * busmode bit must match the card's ACMD6-selected width. */
    MCI_DMACTRL   = MCI_DMACTRL_L2;
    MCI_DATATIMER = 0x00030000u;
    MCI_DATALEN   = 512u;
    MCI_DATACTRL  = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512
                  | (g_bus_4bit ? DPSM_BUS_4BIT : 0u);

    MCI_ARG  = arg;
    MCI_MASK = 0x1FFu;
    MCI_CMD  = CPSM_ENABLE | CPSM_RESPONSE | (17u << 1);

    if (!(wait_sta(RESP_MASK) & STA_RESPEND)) {
        sd_stop_transfer();
        return -1;
    }
    /* Consume R1 so the AK MCI clears its sticky RESPEND latch. */
    (void)MCI_RESP0;

    /* MCI DMAs the 512-byte block into the L2 buffer 64 bytes at a time.
     * L2_BUFSTAT1[id*4 +: 4] counts filled 64-byte chunks; wait for all 8
     * (a full 512-byte block), then bulk-copy from on-chip L2 SRAM. The
     * hot loop touches L2 registers/SRAM, not the slow MCI FIFO. */
    uint32_t spins = 0;
    while (((L2_BUFSTAT1 >> (SD_L2_BUF_ID * 4)) & 0xFu) < 8u) {
        if (++spins >= WAIT_LIMIT) {
            sd_stop_transfer();
            return -2;
        }
    }

    const volatile uint32_t *src =
        (const volatile uint32_t *)(uintptr_t)SD_L2_BUF_ADDR;
    uint32_t *dw = (uint32_t *)dst;
    for (uint32_t w = 0; w < 128u; w++)
        dw[w] = src[w];

    sd_stop_transfer();
    return 0;
}
