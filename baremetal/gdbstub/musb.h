#pragma once
#include <stdint.h>

#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* Word access into a byte array, without a break of strict aliasing. */
typedef uint32_t u32_alias __attribute__((may_alias));

/* SYSCTRL */
#define SYSCTRL             0x08000000u
#define CLK_CON1            (SYSCTRL + 0x0Cu)   /* bit15 UDC clock gate, bit31 USB reset */
#define INT_STAT            (SYSCTRL + 0xCCu)   /* bit25 USB event pending */
#define MULFUN_CON1         (SYSCTRL + 0x58u)   /* low 3 bits = 6 selects USB */

#define CLK_CON1_UDC_GATE   (1u << 15)
#define CLK_CON1_USB_RESET  (1u << 31)

/* L2 controller */
#define L2CTR_ASSIGN_REG1   0x2002C090u

/* MUSB core */
#define USB                 0x70000000u
#define USB_FADDR           (USB + 0x00u)
#define USB_POWER           (USB + 0x01u)
#define USB_INTRTX1         (USB + 0x02u)
#define USB_INTRRX1         (USB + 0x04u)
#define USB_INTRTX1E        (USB + 0x06u)
#define USB_INTRRX1E        (USB + 0x08u)
#define USB_INTRUSB         (USB + 0x0Au)
#define USB_INTRUSBE        (USB + 0x0Bu)
#define USB_INDEX           (USB + 0x0Eu)
#define USB_TXMAXP          (USB + 0x10u)
#define USB_CSR0_TXCSR1     (USB + 0x12u)
#define USB_TXCSR2          (USB + 0x13u)
#define USB_RXMAXP          (USB + 0x14u)
#define USB_RXCSR1          (USB + 0x16u)
#define USB_COUNT0_RXCOUNT  (USB + 0x18u)
#define USB_FIFO_EP0        (USB + 0x20u)
#define USB_FIFO_EP2        (USB + 0x28u)

/* Vendor block. Data moves through L2, and these registers gate and start it. */
#define USB_EP0_TX_COUNT    (USB + 0x330u)
#define USB_EP2_TX_COUNT    (USB + 0x334u)
#define USB_FORBID_WRITE    (USB + 0x338u)
#define USB_START_PRE_READ  (USB + 0x33Cu)
#define USB_MODE_STATUS     (USB + 0x344u)

/* Bit 0 sizes the data path for full speed. */
#define MODE_FORCE_FS       (1u << 0)

/* L2 windows that belong to USB, with the low 6 bits of L2CTR_ASSIGN_REG1 = 8. */
#define L2_EP2_TX           0x48000000u   /* bulk IN staging */
#define L2_EP3_RX           0x48000200u   /* bulk OUT landing area */
#define L2_EP0              0x48001500u   /* EP0 both directions */

/* INTRUSB */
#define INTRUSB_RESET       (1u << 2)

/* POWER, peripheral mode. This part has no SOFTCONN (bit 6). */
#define POWER_HSMODE        (1u << 4)   /* read-only, set when the chirp gave HS */
#define POWER_HSENAB        (1u << 5)   /* chirp at the next bus reset */

/* CSR0, peripheral mode */
#define CSR0_RXPKTRDY       (1u << 0)
#define CSR0_TXPKTRDY       (1u << 1)
#define CSR0_SENTSTALL      (1u << 2)
#define CSR0_DATAEND        (1u << 3)
#define CSR0_SETUPEND       (1u << 4)
#define CSR0_SENDSTALL      (1u << 5)
#define CSR0_SERVICED_RX    (1u << 6)
#define CSR0_SERVICED_SETUP (1u << 7)

/* TXCSR1 / RXCSR1 for endpoints other than 0 */
#define TXCSR1_TXPKTRDY     (1u << 0)
#define TXCSR1_UNDERRUN     (1u << 2)
#define TXCSR1_SENDSTALL    (1u << 4)
#define TXCSR1_SENTSTALL    (1u << 5)
#define TXCSR1_CLRDATATOG   (1u << 6)
#define RXCSR1_RXPKTRDY     (1u << 0)
#define RXCSR1_CLRDATATOG   (1u << 7)

#define EP_NOTIFY           1u      /* CDC notification, declared but unused */
#define EP_BULK_IN          2u
#define EP_BULK_OUT         3u

#define NOTIFY_MAXP         16u

/* 64 is valid at both speeds. */
#define EP0_MAXP            64u

/* Bytes staged into the EP0 L2 window per packet. */
#define EP0_STAGE           64u

/* Holds the largest descriptor, rounded up to a whole number of packets. */
#define EP0_BUF_SIZE        128u

/* Software chunks at the negotiated size. TXMAXP and RXMAXP stay at 512. */
#define BULK_MAXP_HS        512u
#define BULK_MAXP_FS        64u

#define TX_WAIT_LIMIT       0x00200000u

void musb_init(void);
void musb_poll(void);
void musb_bulk_send(const uint8_t *data, uint32_t len);

/* Endpoint counters and live register state, for the trace after a failure. */
void musb_report(void);
uint32_t musb_activity(void);
