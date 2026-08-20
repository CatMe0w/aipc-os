/*
 * MUSB device mode for the AK7802, CDC ACM.
 *
 * Data does not move through the FIFO ports. Each payload goes into an L2 SRAM
 * window instead, and a write to the FIFO port only advances the hardware
 * pointer, once per word. The transmit order is FORBID_WRITE, fill L2 and poke
 * the FIFO, TX_COUNT, PRE_READ, CSR ready. Another order kills the endpoint
 * with no error.
 */

#include "musb.h"
#include "rsp.h"
#include "trace.h"

#define VENDOR_ID   0x1209u     /* pid.codes */
#define PRODUCT_ID  0x0001u     /* their test PID */

#define TRACE_EVENT_LIMIT 200u
#define TRACE_CHUNK_LIMIT 40u

static uint8_t g_ep0_buf[EP0_BUF_SIZE] __attribute__((aligned(4)));
static uint32_t g_ep0_remaining;
static uint32_t g_ep0_offset;

/* Non-zero while a host-to-device data stage is outstanding on EP0. */
static uint32_t g_ep0_out_remaining;
static uint32_t g_ep0_out_offset;

/* Echoed back in GET_LINE_CODING. None of it reaches real hardware. */
static uint8_t g_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };
static uint16_t g_line_state;

static uint32_t g_events;
static uint32_t g_ep0_chunks;

/* Latched from POWER.HSMODE on every bus reset. */
static uint32_t g_high_speed;
static uint32_t g_bulk_maxp = BULK_MAXP_FS;

static uint32_t g_stat_reset;
static uint32_t g_stat_setup;
static uint32_t g_stat_stall;
static uint32_t g_stat_out;
static uint32_t g_stat_in;
static uint32_t g_stat_txdrop;

static const uint8_t device_desc[18] = {
    18, 1,
    0x00, 0x02,             /* bcdUSB 2.00 */
    0x02, 0x00, 0x00,       /* communications device */
    EP0_MAXP,
    VENDOR_ID & 0xFF, VENDOR_ID >> 8,
    PRODUCT_ID & 0xFF, PRODUCT_ID >> 8,
    0x00, 0x01,             /* bcdDevice 1.00 */
    0, 1, 2,                /* iManufacturer, iProduct, iSerialNumber */
    1
};

/* How the device looks at the other speed. An HS capable device must have it. */
static const uint8_t qualifier_desc[10] = {
    10, 6,
    0x00, 0x02,             /* bcdUSB 2.00 */
    0x02, 0x00, 0x00,       /* communications device */
    EP0_MAXP,
    1,                      /* bNumConfigurations */
    0                       /* bReserved */
};

static const uint8_t string_langid[4] = { 4, 3, 0x09, 0x04 };

static const uint8_t string_product[26] = {
    26, 3,
    'a', 0, 'i', 0, 'p', 0, 'c', 0, ' ', 0,
    'g', 0, 'd', 0, 'b', 0, 's', 0, 't', 0, 'u', 0, 'b', 0
};

/* Stable port name on the host. */
static const uint8_t string_serial[10] = {
    10, 3,
    '0', 0, '0', 0, '0', 0, '1', 0
};

/* A template. Each request patches the descriptor type and the bulk packet
 * sizes into a copy of it. */
static const uint8_t config_desc[67] = {
    9, 2,
    67, 0,                  /* wTotalLength */
    2, 1, 0,
    0xC0, 0x01,             /* self powered, 2 mA: what the bootrom declares */

    9, 4,
    0, 0, 1,                /* interface 0, alt 0, 1 endpoint */
    0x02, 0x02, 0x01, 0,    /* CDC communications, ACM, AT commands */

    5, 0x24, 0x00,          /* CDC header */
    0x10, 0x01,             /* bcdCDC 1.10 */

    5, 0x24, 0x01,          /* call management */
    0x00,                   /* not handled by the device */
    1,                      /* data interface */

    4, 0x24, 0x02,          /* ACM functional */
    0x02,                   /* line coding and serial state supported */

    5, 0x24, 0x06,          /* union */
    0, 1,                   /* control interface, subordinate interface */

    7, 5,
    0x80 | EP_NOTIFY, 3,
    NOTIFY_MAXP, 0, 9,

    9, 4,
    1, 0, 2,                /* interface 1, alt 0, 2 endpoints */
    0x0A, 0x00, 0x00, 0,    /* CDC data */

    7, 5,
    EP_BULK_OUT, 2,
    0, 0, 0,                /* wMaxPacketSize patched */

    7, 5,
    0x80 | EP_BULK_IN, 2,
    0, 0, 0                 /* wMaxPacketSize patched */
};

#define CFG_BULK_OUT_MAXP   57u
#define CFG_BULK_IN_MAXP    64u

_Static_assert(sizeof(config_desc) <= EP0_BUF_SIZE, "config descriptor too big");

/* CDC class requests, interface recipient. */
#define CDC_SET_LINE_CODING         0x20u
#define CDC_GET_LINE_CODING         0x21u
#define CDC_SET_CONTROL_LINE_STATE  0x22u
#define CDC_SEND_BREAK              0x23u

static uint32_t traceable(void)
{
    return g_events < TRACE_EVENT_LIMIT;
}

/* HSMODE survives a soft reset. A stub that another stub started thus reads
 * the speed of the session that it inherited. */
static void latch_speed(uint8_t power)
{
    g_high_speed = (power & POWER_HSMODE) ? 1u : 0u;
    g_bulk_maxp = g_high_speed ? BULK_MAXP_HS : BULK_MAXP_FS;
}

void musb_init(void)
{
    uint32_t v;

    /* This part has no SOFTCONN. The pull-up survives everything, so software
     * cannot signal a disconnect. */
    REG32(CLK_CON1) &= ~CLK_CON1_UDC_GATE;

    REG32(CLK_CON1) |= CLK_CON1_USB_RESET;
    REG32(CLK_CON1) &= ~CLK_CON1_USB_RESET;

    REG32(MULFUN_CON1) &= ~7u;
    REG32(MULFUN_CON1) |= 6u;

    v = REG32(L2CTR_ASSIGN_REG1);
    REG32(L2CTR_ASSIGN_REG1) = v & ~0x3Fu;
    REG32(L2CTR_ASSIGN_REG1) = (v & ~0x3Fu) | 8u;

    /* MODE_FORCE_FS sizes the data path and HSENAB arms the chirp. Both are
     * necessary. Set HSENAB before the host reset arrives. */
    REG32(USB_MODE_STATUS) &= ~MODE_FORCE_FS;
    REG8(USB_POWER) = POWER_HSENAB;
    latch_speed(REG8(USB_POWER));

    trace_reg("clk_con1", REG32(CLK_CON1));
    trace_reg("mulfun_con1", REG32(MULFUN_CON1));
    trace_reg("l2_assign1", REG32(L2CTR_ASSIGN_REG1));
    trace_reg("mode_status", REG32(USB_MODE_STATUS));
    trace_reg("power", REG8(USB_POWER));
}

static void configure_endpoint(uint8_t ep, uint16_t maxp, uint32_t is_tx)
{
    REG8(USB_INDEX) = ep;
    if (is_tx) {
        REG8(USB_TXCSR2) = 0x20;        /* MODE = TX on the shared FIFO */
        REG16(USB_TXMAXP) = maxp;
    } else {
        REG8(USB_TXCSR2) = 0;
        REG16(USB_RXMAXP) = maxp;
        REG8(USB_RXCSR1) &= ~RXCSR1_RXPKTRDY;
    }
}

static void handle_bus_reset(void)
{
    /* The reset interrupt fires after the chirp, thus POWER already holds the
     * negotiated speed. */
    uint8_t power = REG8(USB_POWER);

    latch_speed(power);

    REG8(USB_FADDR) = 0;
    REG8(USB_POWER) = POWER_HSENAB;
    REG8(USB_INTRUSBE) = 0xF7;
    REG8(USB_INTRTX1E) = 0x05;          /* EP0 and EP2 */
    REG8(USB_INTRRX1E) = 0x0A;          /* EP1 and EP3 */

    /* Always 512. The L2 staging path works with no other value. Software
     * holds full speed to 64 by chunking instead. */
    configure_endpoint(EP_BULK_IN, 512, 1);
    configure_endpoint(EP_BULK_OUT, 512, 0);

    /* EP_NOTIFY stays unconfigured. It then NAKs, which is correct for a
     * CDC device with nothing to report. */
    REG8(USB_INDEX) = 0;

    g_ep0_remaining = 0;
    g_ep0_offset = 0;
    g_ep0_out_remaining = 0;
    g_stat_reset++;

    if (traceable()) {
        trace_puts("bus reset, power=");
        trace_hex(power, 2);
        trace_puts(g_high_speed ? " HIGH speed\n" : " full speed\n");
    }
}

static void ep0_stall(void)
{
    REG8(USB_INDEX) = 0;
    REG8(USB_CSR0_TXCSR1) = CSR0_SENDSTALL | CSR0_SERVICED_RX;
    g_stat_stall++;
}

/* L2 always gets EP0_STAGE bytes. Only the FIFO poke count and TX_COUNT follow
 * the real packet length. */
static void ep0_send_chunk(void)
{
    const uint8_t *src = &g_ep0_buf[g_ep0_offset];
    uint32_t count = (g_ep0_remaining > EP0_MAXP) ? EP0_MAXP : g_ep0_remaining;
    uint32_t last = (g_ep0_remaining <= EP0_MAXP);
    uint32_t i;

    REG8(USB_INDEX) = 0;

    if (!g_ep0_remaining) {
        g_ep0_offset = 0;
        REG8(USB_CSR0_TXCSR1) = CSR0_SERVICED_RX | CSR0_DATAEND;
        return;
    }

    REG32(USB_FORBID_WRITE) |= 1u;

    if (last) {
        /* Final packet. Byte-wide pokes, so that the count matches the
         * packet length. */
        for (i = 0; i < count; i++)
            REG8(USB_FIFO_EP0) = 0;
        for (i = 0; i < EP0_STAGE; i += 4)
            REG32(L2_EP0 + i) = *(const u32_alias *)(src + i);
    } else {
        for (i = 0; i < EP0_STAGE; i += 4) {
            REG32(L2_EP0 + i) = *(const u32_alias *)(src + i);
            REG32(USB_FIFO_EP0) = 0;
        }
    }

    REG32(USB_EP0_TX_COUNT) = count;
    REG32(USB_START_PRE_READ) |= 1u;

    /* TxPktRdy, then DataEnd, as two writes. One combined write fails. */
    REG8(USB_CSR0_TXCSR1) = CSR0_TXPKTRDY;
    if (last)
        REG8(USB_CSR0_TXCSR1) = CSR0_DATAEND;

    if (!last)
        for (i = 0; i < 3; i++)
            __asm__ volatile ("" : : : "memory");

    REG32(USB_FORBID_WRITE) &= ~1u;

    if (g_ep0_chunks < TRACE_CHUNK_LIMIT) {
        g_ep0_chunks++;
        trace_puts("ep0 tx ");
        trace_dec(count);
        trace_puts(last ? " last" : " more");
        trace_puts(" csr=");
        trace_hex(REG8(USB_CSR0_TXCSR1), 2);
        trace_puts(" cnt=");
        trace_hex(REG32(USB_EP0_TX_COUNT), 8);
        trace_puts("\n");
    }

    if (last) {
        g_ep0_remaining = 0;
        g_ep0_offset = 0;
    } else {
        g_ep0_remaining -= count;
        g_ep0_offset += count;
    }
}

static void ep0_fill(const uint8_t *data, uint32_t len, uint32_t wLength)
{
    uint32_t i;

    if (len > wLength)
        len = wLength;
    if (len > sizeof(g_ep0_buf))
        len = sizeof(g_ep0_buf);

    for (i = 0; i < sizeof(g_ep0_buf); i++)
        g_ep0_buf[i] = (i < len) ? data[i] : 0;

    g_ep0_remaining = len;
    g_ep0_offset = 0;
}

static void ep0_start(void)
{
    REG8(USB_INDEX) = 0;
    REG8(USB_CSR0_TXCSR1) = CSR0_SERVICED_RX;   /* acknowledge the SETUP */
    ep0_send_chunk();
}

static void ep0_queue(const uint8_t *data, uint32_t len, uint32_t wLength)
{
    ep0_fill(data, len, wLength);
    ep0_start();
}

/* Patch descriptor type and bulk maxp for the requested speed. */
static void ep0_queue_config(uint8_t type, uint32_t bulk_maxp, uint32_t wLength)
{
    ep0_fill(config_desc, sizeof(config_desc), wLength);

    g_ep0_buf[1] = type;
    g_ep0_buf[CFG_BULK_IN_MAXP] = (uint8_t)bulk_maxp;
    g_ep0_buf[CFG_BULK_IN_MAXP + 1] = (uint8_t)(bulk_maxp >> 8);
    g_ep0_buf[CFG_BULK_OUT_MAXP] = (uint8_t)bulk_maxp;
    g_ep0_buf[CFG_BULK_OUT_MAXP + 1] = (uint8_t)(bulk_maxp >> 8);

    ep0_start();
}

static void ep0_status(void)
{
    g_ep0_remaining = 0;
    g_ep0_offset = 0;
    REG8(USB_INDEX) = 0;
    REG8(USB_CSR0_TXCSR1) = CSR0_SERVICED_RX | CSR0_DATAEND;
}

/* Accept a host-to-device data stage. */
static void ep0_expect_out(uint32_t wLength)
{
    g_ep0_remaining = 0;
    g_ep0_offset = 0;
    g_ep0_out_remaining = wLength;
    g_ep0_out_offset = 0;
    REG8(USB_INDEX) = 0;
    REG8(USB_CSR0_TXCSR1) = CSR0_SERVICED_RX;
}

/* Only SET_LINE_CODING gets here. */
static void handle_ep0_out(void)
{
    uint32_t count;
    uint32_t i;

    REG8(USB_INDEX) = 0;
    count = REG32(USB_COUNT0_RXCOUNT) & 0x7Fu;

    for (i = 0; i < count && g_ep0_out_offset < sizeof(g_line_coding); i++)
        g_line_coding[g_ep0_out_offset++] = REG8(L2_EP0 + i);

    g_ep0_out_remaining = (count >= g_ep0_out_remaining)
                          ? 0 : g_ep0_out_remaining - count;

    REG8(USB_CSR0_TXCSR1) = g_ep0_out_remaining
                            ? CSR0_SERVICED_RX
                            : (uint8_t)(CSR0_SERVICED_RX | CSR0_DATAEND);
}

static void handle_class_setup(uint8_t bRequest, uint16_t wValue,
                               uint16_t wLength)
{
    switch (bRequest) {
    case CDC_SET_LINE_CODING:
        if (wLength)
            ep0_expect_out(wLength);
        else
            ep0_status();
        break;

    case CDC_GET_LINE_CODING:
        ep0_queue(g_line_coding, sizeof(g_line_coding), wLength);
        break;

    case CDC_SET_CONTROL_LINE_STATE:
        g_line_state = wValue;              /* bit 0 DTR, bit 1 RTS */
        ep0_status();
        break;

    case CDC_SEND_BREAK:
        ep0_status();
        break;

    default:
        ep0_stall();
        break;
    }
}

static void handle_setup(void)
{
    uint8_t bmRequestType = REG8(L2_EP0 + 0);
    uint8_t bRequest = REG8(L2_EP0 + 1);
    uint16_t wValue = (uint16_t)(REG8(L2_EP0 + 2) | (REG8(L2_EP0 + 3) << 8));
    uint16_t wIndex = (uint16_t)(REG8(L2_EP0 + 4) | (REG8(L2_EP0 + 5) << 8));
    uint16_t wLength = (uint16_t)(REG8(L2_EP0 + 6) | (REG8(L2_EP0 + 7) << 8));
    uint8_t reply[2];
    uint32_t i;

    (void)wIndex;
    g_stat_setup++;

    if (traceable()) {
        trace_puts("setup ");
        trace_hex(bmRequestType, 2);
        trace_puts(" ");
        trace_hex(bRequest, 2);
        trace_puts(" v");
        trace_hex(wValue, 4);
        trace_puts(" i");
        trace_hex(wIndex, 4);
        trace_puts(" l");
        trace_hex(wLength, 4);
        trace_puts("\n");
    }

    if ((bmRequestType & 0x60u) == 0x20u) {     /* class: CDC */
        handle_class_setup(bRequest, wValue, wLength);
        return;
    }
    if ((bmRequestType & 0x60u) != 0) {         /* vendor: none */
        ep0_stall();
        return;
    }

    switch (bRequest) {
    case 0:                                     /* GET_STATUS */
        reply[0] = 1;                           /* self powered, as declared */
        reply[1] = 0;
        ep0_queue(reply, 2, wLength);
        break;

    case 5:                                     /* SET_ADDRESS */
        /* Wait for the EP0 transmit to finish before the FADDR write. */
        ep0_status();
        for (i = 0; i < 10000u && !(REG8(USB_INTRTX1) & 0x01u); i++)
            ;
        REG8(USB_FADDR) = (uint8_t)(wValue & 0x7Fu);
        if (traceable())
            trace_reg("faddr", wValue & 0x7Fu);
        break;

    case 6:                                     /* GET_DESCRIPTOR */
        switch (wValue >> 8) {
        case 1:                                 /* DEVICE */
            ep0_queue(device_desc, sizeof(device_desc), wLength);
            break;
        case 2:                                 /* CONFIGURATION */
            ep0_queue_config(2, g_bulk_maxp, wLength);
            break;
        case 3:                                 /* STRING */
            switch (wValue & 0xFFu) {
            case 0:
                ep0_queue(string_langid, sizeof(string_langid), wLength);
                break;
            case 1:
                ep0_queue(string_product, sizeof(string_product), wLength);
                break;
            case 2:
                ep0_queue(string_serial, sizeof(string_serial), wLength);
                break;
            default:
                ep0_stall();
                break;
            }
            break;
        case 6:                                 /* DEVICE_QUALIFIER */
            ep0_queue(qualifier_desc, sizeof(qualifier_desc), wLength);
            break;
        case 7:                                 /* OTHER_SPEED_CONFIGURATION */
            ep0_queue_config(7, g_high_speed ? BULK_MAXP_FS : BULK_MAXP_HS,
                             wLength);
            break;
        default:
            ep0_stall();
            break;
        }
        break;

    case 8:                                     /* GET_CONFIGURATION */
        reply[0] = 1;
        ep0_queue(reply, 1, wLength);
        break;

    case 9:                                     /* SET_CONFIGURATION */
        REG8(USB_INDEX) = EP_BULK_IN;
        REG8(USB_CSR0_TXCSR1) = TXCSR1_CLRDATATOG;
        REG8(USB_INDEX) = EP_BULK_OUT;
        REG8(USB_RXCSR1) = RXCSR1_CLRDATATOG;
        REG8(USB_INDEX) = 0;
        ep0_status();
        break;

    case 1:                                     /* CLEAR_FEATURE */
    case 3:                                     /* SET_FEATURE */
    case 11:                                    /* SET_INTERFACE */
        ep0_status();
        break;

    case 10:                                    /* GET_INTERFACE */
        reply[0] = 0;
        ep0_queue(reply, 1, wLength);
        break;

    default:
        ep0_stall();
        break;
    }
}

/* Bounded so a vanished host cannot wedge the stub. */
static uint32_t bulk_tx_wait(void)
{
    uint32_t n;

    REG8(USB_INDEX) = EP_BULK_IN;
    for (n = 0; n < TX_WAIT_LIMIT; n++)
        if (!(REG8(USB_CSR0_TXCSR1) & TXCSR1_TXPKTRDY))
            return 1;

    g_stat_txdrop++;
    return 0;
}

static uint32_t pack_le(const uint8_t *p, uint32_t avail)
{
    uint32_t w = 0;
    uint32_t k;

    for (k = 0; k < 4; k++)
        w |= (uint32_t)((k < avail) ? p[k] : 0) << (8 * k);
    return w;
}

/* Word load when aligned, byte-by-byte otherwise. */
static uint32_t load_le(const uint8_t *p, uint32_t avail)
{
    if (avail >= 4 && !((uintptr_t)p & 3u))
        return *(const u32_alias *)p;
    return pack_le(p, avail);
}

void musb_bulk_send(const uint8_t *data, uint32_t len)
{
    while (len) {
        uint32_t count = (len > g_bulk_maxp) ? g_bulk_maxp : len;
        uint32_t i;

        /* A full final packet would sit in the URB of the host, so shorten
         * it by one byte. */
        if (count == len && count == g_bulk_maxp)
            count--;

        if (!bulk_tx_wait())
            return;

        REG8(USB_INDEX) = EP_BULK_IN;
        REG32(USB_FORBID_WRITE) |= 2u;

        /* The wire length follows the FIFO pointer, not TX_COUNT. A length
         * that is not a whole number of words needs byte-wide pokes. */
        if (count & 3u) {
            for (i = 0; i < count; i++)
                REG8(USB_FIFO_EP2) = 0;
            for (i = 0; i < count; i += 4)
                REG32(L2_EP2_TX + i) = load_le(data + i, count - i);
        } else {
            for (i = 0; i < count; i += 4) {
                REG32(L2_EP2_TX + i) = load_le(data + i, count - i);
                REG32(USB_FIFO_EP2) = 0;
            }
        }

        REG32(USB_EP2_TX_COUNT) = count;
        REG32(USB_START_PRE_READ) |= 2u;
        REG8(USB_CSR0_TXCSR1) = TXCSR1_TXPKTRDY;
        REG32(USB_FORBID_WRITE) &= ~2u;

        data += count;
        len -= count;
        g_stat_in++;
    }
}

/* Words, not bytes. This avoids a second pass with the MMU off. */
static uint32_t g_rx_buf[BULK_MAXP_HS / 4];

static void handle_bulk_out(void)
{
    uint32_t count;
    uint32_t i;

    REG8(USB_INDEX) = EP_BULK_OUT;
    count = REG32(USB_COUNT0_RXCOUNT) & 0xFFFFu;
    if (count > sizeof(g_rx_buf))
        count = sizeof(g_rx_buf);

    for (i = 0; i < count; i += 4)
        g_rx_buf[i >> 2] = REG32(L2_EP3_RX + i);

    /* Read RXCSR1 again. An older snapshot of it loses packets. */
    REG8(USB_INDEX) = EP_BULK_OUT;
    REG8(USB_RXCSR1) = (uint8_t)(REG8(USB_RXCSR1) & ~RXCSR1_RXPKTRDY);

    g_stat_out++;
    rsp_feed((const uint8_t *)g_rx_buf, count);
}

void musb_poll(void)
{
    /* Sample once. A second read part way through the dispatch can lose a
     * bit. */
    uint8_t intrrx = REG8(USB_INTRRX1);
    uint8_t intrtx = REG8(USB_INTRTX1);
    uint8_t intrusb = REG8(USB_INTRUSB);
    uint8_t csr;

    if (intrusb & INTRUSB_RESET) {
        g_events++;
        handle_bus_reset();
        return;
    }

    if (intrtx & 0x01u) {
        g_events++;
        REG8(USB_INDEX) = 0;
        csr = REG8(USB_CSR0_TXCSR1);

        if (traceable())
            trace_reg("ep0 evt csr", csr);

        if (csr & CSR0_SETUPEND) {
            g_ep0_out_remaining = 0;            /* the host dropped the transfer */
            REG8(USB_CSR0_TXCSR1) = CSR0_SERVICED_SETUP;
        } else if (csr & CSR0_SENTSTALL) {
            REG8(USB_CSR0_TXCSR1) = (uint8_t)(csr & ~CSR0_SENTSTALL);
        } else if (csr & CSR0_RXPKTRDY) {
            if (g_ep0_out_remaining)
                handle_ep0_out();
            else
                handle_setup();
        } else if (!(csr & CSR0_TXPKTRDY)) {
            if (g_ep0_remaining)
                ep0_send_chunk();
        }
        return;
    }

    if (intrtx & 0x04u) {
        g_events++;
        REG8(USB_INDEX) = EP_BULK_IN;
        csr = REG8(USB_CSR0_TXCSR1);
        if (csr & TXCSR1_UNDERRUN)
            REG8(USB_CSR0_TXCSR1) = TXCSR1_TXPKTRDY;
        else if (csr & TXCSR1_SENTSTALL)
            REG8(USB_CSR0_TXCSR1) = (uint8_t)(csr & ~TXCSR1_SENTSTALL);
    }

    if (intrrx & 0x08u) {
        g_events++;
        REG8(USB_INDEX) = EP_BULK_OUT;
        if (REG8(USB_RXCSR1) & RXCSR1_RXPKTRDY)
            handle_bulk_out();
    }
}

uint32_t musb_activity(void)
{
    return g_stat_reset + g_stat_setup + g_stat_stall + g_stat_out + g_stat_in +
           g_stat_txdrop;
}

void musb_report(void)
{
    trace_puts("--- counters ---\n");
    trace_reg("events", g_events);
    trace_reg("resets", g_stat_reset);
    trace_reg("setups", g_stat_setup);
    trace_reg("stalls", g_stat_stall);
    trace_reg("bulk_out", g_stat_out);
    trace_reg("bulk_in", g_stat_in);
    trace_reg("tx_drops", g_stat_txdrop);
    trace_reg("high_speed", g_high_speed);
    trace_reg("bulk_maxp", g_bulk_maxp);
    trace_reg("line_state", g_line_state);
    /* The interrupt status registers clear on read. Do not add them here. */
    trace_reg("faddr", REG8(USB_FADDR));
    trace_reg("power", REG8(USB_POWER));
    trace_reg("mode_status", REG32(USB_MODE_STATUS));
    trace_reg("int_stat", REG32(INT_STAT));
}
