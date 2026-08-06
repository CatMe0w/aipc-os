#include "soc.h"

#define REG32(a)  (*(volatile uint32_t *)(uintptr_t)(a))

#define SYSCTRL_SHAREPIN2   REG32(0x08000074u)
#define SYSCTRL_SHAREPIN1   REG32(0x08000078u)

#define UART_CTRL           REG32(0x20026000u)
#define UART_CFG            REG32(0x20026004u)
#define UART_COUNT          REG32(0x20026008u)
#define UART_RX_THRESH      REG32(0x2002600Cu)

#define L2CTR_UART_CFG      REG32(0x2002C040u)
#define L2CTR_UART_PORT_CFG REG32(0x2002C04Cu)
#define L2CTR_DMA_PATH_CFG  REG32(0x2002C084u)
#define L2CTR_BUF0_7_CFG    REG32(0x2002C088u)
#define L2CTR_ASSIGN_REG1   REG32(0x2002C090u)

#define L2_UART_TX_PORT     REG32(0x48001000u)
#define L2_UART_TX_FRAC     REG32(0x4800103Cu)

#define NF_TIMING0_BLK0     REG32(0x2002A05Cu)

#define UART_TX_LIMIT       0x00100000u

#define EBOOT_ENTRY         0x30038000
#define EBOOT_SP            0x30036000

#define STR_(x)         #x
#define STR(x)          STR_(x)

/* Only write the divider if the controller is unconfigured; v1.88's bootrom
 * sets a different one and overwriting it would change the baud rate. */
void uart_init(void)
{
    SYSCTRL_SHAREPIN1 |= 0x00000200u;
    L2CTR_UART_CFG    |= 0x00000800u;
    if (UART_CTRL == 0u)
        UART_CTRL = 0x30200208u;
    UART_RX_THRESH = 0u;
}

void uart_putc(char c)
{
    L2CTR_UART_PORT_CFG |= 0x00010000u;
    L2_UART_TX_PORT = (uint32_t)(uint8_t)c;
    L2_UART_TX_FRAC = 0u;
    UART_CTRL |= 0x10000000u;
    UART_CFG  |= 0x00010010u;
    /* Bounded: avoid a bad UART from hanging the system */
    for (uint32_t i = 0; i < UART_TX_LIMIT && (UART_COUNT & 0x1FFFu) != 0u; ++i)
        ;
}

/* Both NAND and MMC stall if bits 29:28 are clear. The bootrom sets them on
 * a real boot, but in USB boot mode nothing has. */
void l2_init(void)
{
    L2CTR_DMA_PATH_CFG |= 0x30000000u;
}

void nf_hw_init(void)
{
    SYSCTRL_SHAREPIN2  = (SYSCTRL_SHAREPIN2 & ~0x00000018u) | 0x00000008u;
    SYSCTRL_SHAREPIN1 |= 0x00C70200u;
    L2CTR_ASSIGN_REG1 &= ~0x00000E00u;
    L2CTR_BUF0_7_CFG  |= 0x00010000u;
    L2CTR_BUF0_7_CFG  |= 0x01000000u;
    l2_init();
    NF_TIMING0_BLK0 = 0x000F5BD1u;
}

/* Reload SP (our call frames moved it) and unmask IRQ/FIQ to reproduce the
 * state the OEM nboot leaves for EBOOT. */
void handoff_eboot(void)
{
    __asm__ volatile (
        "ldr sp, =" STR(EBOOT_SP) "\n"
        "msr cpsr_fc, #0x13\n"
        "ldr pc, =" STR(EBOOT_ENTRY) "\n"
        ::: "memory"
    );
    __builtin_unreachable();
}

void handoff_bare(uint32_t addr)
{
    __asm__ volatile (
        "mov pc, %0\n"
        :: "r" (addr) : "memory"
    );
    __builtin_unreachable();
}
