/*
 * AK7802 NAND dump stub
 *
 * Uploaded to L2BUF_01 (0x48000200) via USB boot mode, then executed.
 * Takes over the existing USB connection to:
 *   1. Initialize the NAND flash controller
 *   2. Read the NAND chip ID and detect geometry
 *   3. Send a 64-byte info header to the host
 *   4. Stream all NAND pages back to the host via USB bulk IN
 *
 * Memory layout (6 KB; bootrom only accesses up to 0x480017FF):
 *   0x48000000 - 0x480001FF  L2BUF_00  NAND DMA target / USB TX staging
 *   0x48000200 - 0x48000DFF  stub code (3 KB)
 *   0x48000E00 - 0x48000FFF  NAND read temp buffer (512 bytes)
 *   0x48001000 - 0x480017F0  stack (~2 KB)
 */

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Register access helpers                                            */
/* ------------------------------------------------------------------ */

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))

/* ------------------------------------------------------------------ */
/* Hardware addresses                                                 */
/* ------------------------------------------------------------------ */

/* System control */
#define SYSCTRL             0x08000000
#define SYSCTRL_SHAREPIN0   REG32(SYSCTRL + 0x74)
#define SYSCTRL_SHAREPIN1   REG32(SYSCTRL + 0x78)
#define SYSCTRL_USB_INT     REG32(SYSCTRL + 0xCC)

/* NAND flash sequencer */
#define NF_TIMING0          REG32(0x2002A05C)
#define NF_SEQ_CTRL_STA     REG32(0x2002A100)
#define NF_SEQ_WORD(n)      REG32(0x2002A104u + (unsigned)(n) * 4u)

/* NAND flash DMA */
#define NF_DMA_CTRL         REG32(0x2002B000)

/* L2 buffer control */
#define L2CTR_COMBUF_CFG    REG32(0x2002C080)
#define L2CTR_ASSIGN_REG1   REG32(0x2002C088)
#define L2CTR_DMAFRAC       REG32(0x2002C08C)
#define L2CTR_ASSIGN_ALT    REG32(0x2002C090)

/* L2 buffer SRAM */
#define L2BUF_00            ((volatile uint32_t *)0x48000000)
#define TEMP_BUF            ((uint32_t *)0x48000E00)

/* USB controller (MUSBMHDRC-like) */
#define USB_INDEX           REG8 (0x7000000E)
#define USB_TXCSR1          REG8 (0x70000012)
#define USB_FIFO_EP2        REG32(0x70000028)
#define USB_EP2_TX_COUNT    REG32(0x70000334)
#define USB_FORBID_WRITE    REG32(0x70000338)
#define USB_PRE_READ        REG32(0x7000033C)

/* ------------------------------------------------------------------ */
/* NAND geometry                                                      */
/* ------------------------------------------------------------------ */

struct nand_geo {
    uint32_t page_size;     /* bytes (data area only) */
    uint32_t block_size;    /* bytes */
    uint32_t total_size;    /* total capacity in bytes */
    uint32_t addr_cycles;   /* total address cycles for a page read */
    uint32_t total_pages;
    int      large_page;    /* 1 = large page (cmd 0x00/0x30), 0 = small */
};

/* ------------------------------------------------------------------ */
/* Info header sent to host (64 bytes)                                */
/* ------------------------------------------------------------------ */

struct __attribute__((packed)) dump_header {
    uint32_t magic;             /* 0x444E414E  "NAND" little-endian */
    uint8_t  id_bytes[8];       /* raw Read ID response */
    uint32_t page_size;
    uint32_t block_size;
    uint32_t total_size;
    uint32_t addr_cycles;
    uint32_t total_pages;
    uint32_t flags;             /* bit 0: auto-detect succeeded */
    uint8_t  reserved[24];
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void delay(volatile int n)
{
    while (n-- > 0)
        ;
}

static void memcpy32(uint32_t *dst, const volatile uint32_t *src, int nwords)
{
    for (int i = 0; i < nwords; i++)
        dst[i] = src[i];
}

static void memzero(void *dst, int nbytes)
{
    uint8_t *p = (uint8_t *)dst;
    for (int i = 0; i < nbytes; i++)
        p[i] = 0;
}

/* Count trailing zeros (log2 for powers of two).  Avoids __aeabi_uidiv. */
static int ctz(uint32_t v)
{
    int n = 0;
    while (v && !(v & 1)) { v >>= 1; n++; }
    return n;
}

/* ------------------------------------------------------------------ */
/* NF sequencer helpers                                               */
/* ------------------------------------------------------------------ */

static void nf_seq_exec(void)
{
    NF_SEQ_CTRL_STA = 0x40000600;
    while (!(NF_SEQ_CTRL_STA & (1u << 31)))
        ;
}

/*
 * DMA-read byte_count bytes from the NAND data bus into L2BUF_00.
 * Must be called after the command/address sequence has completed.
 * Caller is responsible for flushing L2CTR_COMBUF_CFG afterwards.
 */
static void nf_dma_read(int byte_count)
{
    NF_DMA_CTRL = ((uint32_t)byte_count << 7) | 0x100018;

    NF_SEQ_CTRL_STA = 0;
    NF_SEQ_WORD(0)  = (((uint32_t)(byte_count - 1)) << 11) | 0x119;
    nf_seq_exec();

    while (!(NF_DMA_CTRL & (1u << 6)))
        ;
    NF_DMA_CTRL |= (1u << 6);   /* write-1-to-clear done bit */
}

/* ------------------------------------------------------------------ */
/* NAND hardware init (mirrors bootrom nf_boot_hw_init)               */
/* ------------------------------------------------------------------ */

static void nand_hw_init(void)
{
    /* Sharepin: select NF function */
    uint32_t v = SYSCTRL_SHAREPIN0;
    v &= ~(3u << 3);
    v |=  (1u << 3);
    SYSCTRL_SHAREPIN0 = v;

    v = SYSCTRL_SHAREPIN1;
    v |= 0xC70200;
    SYSCTRL_SHAREPIN1 = v;

    /* L2 buffer assignment for NF path */
    v = L2CTR_ASSIGN_ALT;
    v &= ~(7u << 9);
    L2CTR_ASSIGN_ALT = v;

    /* Enable and flush common buffer */
    L2CTR_COMBUF_CFG |= (1u << 16);
    L2CTR_COMBUF_CFG |= (1u << 24);

    /* DMA fractional config */
    L2CTR_DMAFRAC |= (3u << 28);

    /* Default NF timing */
    NF_TIMING0 = 0x0F5B51;
}

/* ------------------------------------------------------------------ */
/* NAND Read ID                                                       */
/* ------------------------------------------------------------------ */

static void nand_read_id(uint8_t *buf)
{
    /* Phase 1: command 0x90, address 0x00 */
    NF_SEQ_CTRL_STA = 0;
    NF_SEQ_WORD(0)  = (0x90u << 11) | 0x64;   /* cmd 0x90 */
    NF_SEQ_WORD(1)  = (0x00u << 11) | 0x62;   /* addr 0x00 */
    NF_SEQ_WORD(2)  = (10u   << 11) | 0x401;  /* wait 10 ticks */
    nf_seq_exec();

    /* Phase 2: DMA-read 8 bytes from data bus */
    nf_dma_read(8);

    /* Copy out of L2BUF_00 */
    uint32_t w0 = L2BUF_00[0];
    uint32_t w1 = L2BUF_00[1];
    buf[0] = (uint8_t)(w0);
    buf[1] = (uint8_t)(w0 >> 8);
    buf[2] = (uint8_t)(w0 >> 16);
    buf[3] = (uint8_t)(w0 >> 24);
    buf[4] = (uint8_t)(w1);
    buf[5] = (uint8_t)(w1 >> 8);
    buf[6] = (uint8_t)(w1 >> 16);
    buf[7] = (uint8_t)(w1 >> 24);

    L2CTR_COMBUF_CFG |= (1u << 24);   /* flush */
}

/* ------------------------------------------------------------------ */
/* NAND geometry detection from Read ID bytes                         */
/* ------------------------------------------------------------------ */

static int detect_geometry(const uint8_t *id, struct nand_geo *geo)
{
    uint8_t dev_id = id[1];
    uint8_t byte3  = id[3];

    /* Device ID -> total capacity (MB) */
    uint32_t total_mb;
    switch (dev_id) {
    /* Small-page 512 B devices */
    case 0x73: total_mb =   16; break;
    case 0x75: total_mb =   32; break;
    case 0x76: total_mb =   64; break;
    case 0x79: total_mb =  128; break;
    /* Large-page devices */
    case 0xF1: case 0xA1: total_mb =  128; break;
    case 0xDA: case 0xAA: total_mb =  256; break;
    case 0xDC: case 0xAC: total_mb =  512; break;
    case 0xD3: case 0xA3: total_mb = 1024; break;
    case 0xD5: case 0xA5: total_mb = 2048; break;
    case 0xD7:            total_mb = 4096; break;
    default:
        return -1;    /* unknown device */
    }

    /* Small-page override */
    if (dev_id == 0x73 || dev_id == 0x75 ||
        dev_id == 0x76 || dev_id == 0x79) {
        geo->page_size   = 512;
        geo->block_size  = 16384;
        geo->large_page  = 0;
        geo->addr_cycles = (total_mb > 32) ? 4 : 3;
    } else {
        /* Modern large-page: parse byte 3 */
        geo->page_size   = 1024u << (byte3 & 0x03);
        geo->block_size  = 65536u << ((byte3 >> 4) & 0x03);
        geo->large_page  = 1;
        geo->addr_cycles = (total_mb > 128) ? 5 : 4;
    }

    geo->total_size  = total_mb * 1024u * 1024u;
    geo->total_pages = geo->total_size >> ctz(geo->page_size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* NAND page read command                                             */
/* ------------------------------------------------------------------ */

static void nand_issue_page_read(uint32_t page, const struct nand_geo *geo)
{
    NF_SEQ_CTRL_STA = 0;
    int idx = 0;

    if (geo->large_page) {
        /* Large page: cmd 0x00, 2 column + N row cycles, cmd 0x30 */
        NF_SEQ_WORD(idx++) = (0x00u << 11) | 0x64;    /* cmd 0x00 */
        NF_SEQ_WORD(idx++) = (0x00u << 11) | 0x62;    /* column low  = 0 */
        NF_SEQ_WORD(idx++) = (0x00u << 11) | 0x62;    /* column high = 0 */

        int row_cycles = (int)geo->addr_cycles - 2;
        for (int i = 0; i < row_cycles; i++)
            NF_SEQ_WORD(idx++) = (((page >> (8 * i)) & 0xFFu) << 11) | 0x62;

        NF_SEQ_WORD(idx++) = (0x30u << 11) | 0x64;    /* cmd 0x30 */
    } else {
        /* Small page: cmd 0x00, 1 column + N row cycles */
        NF_SEQ_WORD(idx++) = (0x00u << 11) | 0x64;    /* cmd 0x00 */
        NF_SEQ_WORD(idx++) = (0x00u << 11) | 0x62;    /* column = 0 */

        int row_cycles = (int)geo->addr_cycles - 1;
        for (int i = 0; i < row_cycles; i++)
            NF_SEQ_WORD(idx++) = (((page >> (8 * i)) & 0xFFu) << 11) | 0x62;
    }

    /* Wait for NAND internal page read (generous 2000 ticks) */
    NF_SEQ_WORD(idx++) = (2000u << 11) | 0x401;

    nf_seq_exec();
}

/* ------------------------------------------------------------------ */
/* USB bulk IN (EP2) - send one chunk up to 64 bytes                  */
/* ------------------------------------------------------------------ */

/*
 * Replicates the bootrom's usb_bulk_in_send_next_chunk() sequence:
 *   1. Select EP2
 *   2. Set write-forbid to gate L2BUF_00
 *   3. Copy data to L2BUF_00; write dummy 0 to FIFO EP2 per word
 *      (advances internal byte counter for the pre-read DMA)
 *   4. Set EP2_TX_COUNT
 *   5. Trigger pre-read (DMA from L2BUF_00 -> USB TX FIFO)
 *   6. Set TXCSR1 bit 0 (packet ready)
 *   7. Poll until hardware clears TXCSR1 bit 0 (host ACK)
 *   8. Clear write-forbid
 */
static void usb_send_packet(const void *src, int len)
{
    USB_INDEX = 2;

    USB_FORBID_WRITE |= 2u;

    const uint32_t *words = (const uint32_t *)src;
    int nwords = (len + 3) / 4;
    for (int i = 0; i < nwords; i++) {
        L2BUF_00[i] = words[i];
        USB_FIFO_EP2 = 0;
    }

    USB_EP2_TX_COUNT = len;
    USB_PRE_READ    |= 2u;
    USB_TXCSR1       = 1;

    while (USB_TXCSR1 & 1)
        ;

    USB_FORBID_WRITE &= ~2u;
}

/* Send an arbitrarily sized buffer in 64-byte packets. */
static void usb_send_buf(const void *buf, int total)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (total > 0) {
        int chunk = (total > 64) ? 64 : total;
        usb_send_packet(p, chunk);
        p     += chunk;
        total -= chunk;
    }
}

/* Send a zero-length packet to signal end-of-transfer. */
static void usb_send_zlp(void)
{
    USB_INDEX  = 2;
    USB_TXCSR1 = 1;
    while (USB_TXCSR1 & 1)
        ;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* Initialise NAND controller */
    nand_hw_init();
    delay(10000);

    /* Read NAND chip ID */
    uint8_t id[8];
    memzero(id, 8);
    nand_read_id(id);

    /* Detect geometry */
    struct nand_geo geo;
    memzero(&geo, sizeof(geo));
    int detected = detect_geometry(id, &geo);

    /* Build and send info header */
    struct dump_header hdr;
    memzero(&hdr, sizeof(hdr));
    hdr.magic       = 0x444E414E;   /* "NAND" */
    for (int i = 0; i < 8; i++)
        hdr.id_bytes[i] = id[i];
    hdr.page_size   = geo.page_size;
    hdr.block_size  = geo.block_size;
    hdr.total_size  = geo.total_size;
    hdr.addr_cycles = geo.addr_cycles;
    hdr.total_pages = geo.total_pages;
    hdr.flags       = (detected == 0) ? 1 : 0;

    usb_send_packet(&hdr, 64);

    /* If detection failed, stop after sending the header so the host
       can at least inspect the raw ID bytes. */
    if (detected != 0) {
        usb_send_zlp();
        for (;;) ;
    }

    /* Dump all pages */
    uint32_t chunks_per_page = geo.page_size >> 9;  /* / 512 */

    for (uint32_t page = 0; page < geo.total_pages; page++) {
        nand_issue_page_read(page, &geo);

        for (uint32_t c = 0; c < chunks_per_page; c++) {
            /* DMA 512 bytes from NAND data bus -> L2BUF_00 */
            nf_dma_read(512);

            /* Copy L2BUF_00 -> temp buffer (NAND DMA target overlaps
               USB TX staging area, so we must save the data first). */
            memcpy32(TEMP_BUF, L2BUF_00, 128);   /* 512 / 4 = 128 words */

            /* Flush L2 buffer state for next DMA */
            L2CTR_COMBUF_CFG |= (1u << 24);

            /* Stream temp buffer to host via USB, 64 bytes at a time */
            usb_send_buf(TEMP_BUF, 512);
        }
    }

    /* Signal completion */
    usb_send_zlp();

    for (;;) ;
}
