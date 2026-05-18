/*
 * sd_cmd17_probe - verify CMD17 (READ_SINGLE_BLOCK) data path.
 * Reads sector 0 (MBR) from SD card via FIFO, reports first 32 bytes.
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define MCI_BASE      0x20020000u
#define MCI(off)      REG32(MCI_BASE + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_STA       MCI(0x34)
#define MCI_MASK      MCI(0x38)
#define MCI_RESP0     MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALEN   MCI(0x28)
#define MCI_DATACTRL  MCI(0x2C)
#define MCI_FIFO      MCI(0x40)

#define MCI_ENABLE    (1u << 20)
#define MCI_FAIL      (1u << 19)
#define MCI_CLK_EN    (1u << 16)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)

#define CLK_MMC_BIT   (1u << 2)

#define STA_RESP_END    (1u << 4)
#define STA_RESP_TIMEO  (1u << 2)
#define STA_RESP_CRC    (1u << 0)
#define STA_CMD_SENT    (1u << 5)
#define STA_DATA_END    (1u << 6)
#define STA_DATA_BLKEND (1u << 7)
#define STA_FIFO_EMPTY  (1u << 13)

#define DPSM_ENABLE     (1u << 0)
#define DPSM_DIR_READ   (1u << 1)
#define DPSM_BLKSZ_512  (512u << 16)
#define DPSM_BUS_1BIT   (0u << 3)

#define RESP_MASK  (STA_RESP_END | STA_RESP_TIMEO | STA_RESP_CRC)
#define DATA_MASK  (STA_DATA_END | STA_DATA_BLKEND | (1u<<3) | (1u<<1))
#define WAIT_LIMIT 500000u

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 2000u; i++)
        __asm__ volatile ("" : : : "memory");
}

static void clear_result(void)
{
    for (uint32_t i = 0; i < 64u; i++)
        OUT[i] = 0;
}

static uint32_t wait_sta(uint32_t mask)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint32_t sta = REG32(MCI_STA);
        if (sta & mask)
            return sta;
    }
    return 0;
}

void stub_main(void)
{
    clear_result();
    OUT[0] = 0x434D4431u; /* "CMD1" */
    OUT[1] = 1u;

    /* Reset + sharepin + pullup + MCI init (same as proven sequence) */
    uint32_t clk = REG32(SYSCTRL(0x0C));
    REG32(SYSCTRL(0x0C)) = clk | CLK_MMC_BIT; delay();
    REG32(SYSCTRL(0x0C)) = clk & ~CLK_MMC_BIT; delay();

    REG32(SYSCTRL(0x78)) = (REG32(SYSCTRL(0x78)) & ~((7u<<16)|(1u<<29))) | (7u<<16);
    REG32(SYSCTRL(0x74)) = (REG32(SYSCTRL(0x74)) & ~(3u<<3)) | (2u<<3);

    REG32(SYSCTRL(0x9C)) |= 0x180u;
    REG32(SYSCTRL(0xA0)) |= 0x180u;
    REG32(SYSCTRL(0xA4)) |= 0x180u;

    REG32(MCI_CLOCK) = 0; delay();
    REG32(MCI_CLOCK) = MCI_ENABLE | MCI_FAIL; delay();
    REG32(MCI_CLOCK) = MCI_ENABLE | MCI_FAIL | MCI_CLK_EN | 0xF0u;
    delay();
    REG32(MCI_DATATIMER) = 0x00030000u;
    REG32(MCI_MASK) = 0xFFFFFFFFu;

    for (volatile uint32_t i = 0; i < 8000u; i++)
        __asm__ volatile ("" : : : "memory");

    /* CMD0 */
    REG32(MCI_ARG) = 0;
    REG32(MCI_CMD) = CPSM_ENABLE;
    wait_sta(STA_CMD_SENT | STA_RESP_TIMEO);
    delay();

    /* CMD8 */
    REG32(MCI_ARG) = 0x1AAu;
    REG32(MCI_CMD) = CPSM_ENABLE | CPSM_RESPONSE | (8u << 1);
    uint32_t sta8 = wait_sta(RESP_MASK);
    OUT[2] = sta8;
    OUT[3] = REG32(MCI_RESP0);
    if (!(sta8 & (STA_RESP_END | STA_RESP_CRC))) { OUT[4]=0xBAD8; return; }

    /* ACMD41 loop */
    uint32_t ocr_arg = 0x40000000u, ocr = 0;
    for (uint32_t a = 0; a < 200; a++) {
        REG32(MCI_ARG) = 0;
        REG32(MCI_CMD) = CPSM_ENABLE | CPSM_RESPONSE | (55u << 1);
        if (!(wait_sta(RESP_MASK) & (STA_RESP_END | STA_RESP_CRC))) { OUT[4]=0xBAD55|a; return; }

        REG32(MCI_ARG) = ocr_arg;
        REG32(MCI_CMD) = CPSM_ENABLE | CPSM_RESPONSE | (41u << 1);
        uint32_t s = wait_sta(RESP_MASK);
        if (!(s & (STA_RESP_END | STA_RESP_CRC))) { OUT[4]=0xBAD41|a; return; }
        ocr = REG32(MCI_RESP0);
        if (ocr & 0x80000000u) break;
        if (a == 0 && (ocr & 0x00FFFFFFu)) ocr_arg = ocr & 0x40FF8000u;
        delay();
    }
    if (!(ocr & 0x80000000u)) { OUT[4]=0xBAD41FF; return; }

    OUT[4] = ocr;
    int sdhc = (ocr & 0x40000000u) ? 1 : 0;
    OUT[5] = sdhc;

    /* === CMD17: READ_SINGLE_BLOCK (sector 0 = MBR) === */
    wait_sta(STA_FIFO_EMPTY);
    REG32(MCI_DATALEN) = 512u;
    REG32(MCI_DATACTRL) = DPSM_ENABLE | DPSM_DIR_READ | DPSM_BLKSZ_512 | DPSM_BUS_1BIT;

    uint32_t arg = sdhc ? 0u : 0u;  /* block 0 for both SDHC and SDSC */
    REG32(MCI_ARG) = arg;
    REG32(MCI_CMD) = CPSM_ENABLE | CPSM_RESPONSE | (17u << 1);

    uint32_t dsta = wait_sta(DATA_MASK);
    OUT[6] = dsta;

    if (!(dsta & (STA_DATA_END | STA_DATA_BLKEND))) {
        OUT[7] = 0xBAD17u;
        return;
    }

    /* Read 128 words from FIFO into OUT[8..39] */
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)MCI_FIFO;
    for (int i = 0; i < 128; i++) {
        if (i < 32) OUT[8 + i] = fifo[i];
        else (void)fifo[i];  /* discard remaining */
    }

    /* Also dump the first 32 bytes as ASCII for visual check */
    /* Store as packed u32s in OUT[40..47] */
    uint32_t mbr_sig_low  = (uint32_t)OUT[8+127];  /* last word has bytes 508-511 */
    /* Actually, OUT[8] = word 0 = bytes 0-3. Let me extract the MBR signature */
    /* WORD index: byte_off/4. Byte 510 is at word 510/4=127, offset 2 within word */
    uint32_t sig_word = OUT[8+127];  /* bytes 508, 509, 510, 511 */
    uint8_t b510 = (sig_word >> 16) & 0xFFu;
    uint8_t b511 = (sig_word >> 24) & 0xFFu;
    OUT[40] = b510;
    OUT[41] = b511;
}
