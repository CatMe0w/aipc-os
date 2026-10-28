#include "soc.h"
#include "mmio.h"

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

#define SYSCTRL_GPIO1_DIR   REG32(0x0800007Cu)
#define SYSCTRL_GPIO1_IN    REG32(0x080000BCu)
#define POWER_KEY_BIT       0x00000008u   /* GPIO3 = TDO = POWER_KEY */
#define JTAG_MUX_BIT        0x00000001u   /* SHAREPIN1: pins 0..3 are JTAG */

#define SYSCTRL_GPIO3_DIR   REG32(0x0800008Cu)
#define SYSCTRL_GPIO3_OUT   REG32(0x08000090u)
#define AMP_ENABLE_BIT      0x00000020u   /* GPIO69, both speaker amplifiers */

#define SYSCTRL_GPIO4_DIR   REG32(0x08000094u)
#define SYSCTRL_GPIO4_IN    REG32(0x080000C8u)
#define SYSCTRL_GPIO4_OUT   REG32(0x08000098u)
#define POWER_ON_BIT        0x00000200u   /* GPIO105 = DGPIO3 = POWER_ON */
#define WORK_LED_BIT        0x00000100u   /* GPIO104 = DGPIO2 = WORK_STATUS */
#define AC_DET_BIT          0x00000080u   /* GPIO103 = DGPIO1 = AC_DET */
/* The GPIO4 input register is 3 bits behind the other GPIO4 registers. */
#define AC_DET_IN_BIT       (AC_DET_BIT >> 3)

#define UART_TX_LIMIT       0x00100000u

#define EBOOT_ENTRY         0x30038000
#define EBOOT_SP            0x30036000

#define STR_(x)         #x
#define STR(x)          STR_(x)

/* Write the divider only if the controller is unconfigured. A bootrom can
 * set a different one, and an overwrite would change the baud rate. */
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
    /* Bounded, so that a broken UART cannot hang the system. */
    for (uint32_t i = 0; i < UART_TX_LIMIT && (UART_COUNT & 0x1FFFu) != 0u; ++i)
        ;
}

/* Both NAND and MMC stall if bits 29:28 are clear. The bootrom sets them on a
 * real boot, but in USB boot mode nothing sets them. */
void l2_init(void)
{
    L2CTR_DMA_PATH_CFG |= 0x30000000u;
}

/* Latch the 5 V rail on, so that the user can release the power key. */
void power_hold(void)
{
    SYSCTRL_GPIO4_OUT |= POWER_ON_BIT;
    SYSCTRL_GPIO4_DIR &= ~POWER_ON_BIT;
}

/* Drops the latch. The device stays on while the power key is down, or on
 * USB power. */
void power_release(void)
{
    SYSCTRL_GPIO4_OUT &= ~POWER_ON_BIT;
    SYSCTRL_GPIO4_DIR &= ~POWER_ON_BIT;
}

/* The work status LED. Its net also carries the USB_BOOT strap, so the bootrom
 * leaves the pin an input and the LED stays dark until software drives it. */
void work_led(uint32_t on)
{
    if (on)
        SYSCTRL_GPIO4_OUT |= WORK_LED_BIT;
    else
        SYSCTRL_GPIO4_OUT &= ~WORK_LED_BIT;
    SYSCTRL_GPIO4_DIR &= ~WORK_LED_BIT;
}

/* On the v1.58.2 device, AC_DET reads 1 when the battery or the DC adapter is
 * present, and 0 on USB power only. See docs/nk/battery-driver.md. */
uint32_t ac_det(void)
{
    SYSCTRL_GPIO4_DIR |= AC_DET_BIT;
    return (SYSCTRL_GPIO4_IN & AC_DET_IN_BIT) != 0;
}

/* POWER_KEY is low while the key is pressed. The pin is a GPIO only when the
 * JTAG mux bit is clear. See docs/nk/power-management.md. */
uint32_t power_key_down(void)
{
    SYSCTRL_SHAREPIN1 &= ~JTAG_MUX_BIT;
    SYSCTRL_GPIO1_DIR |= POWER_KEY_BIT;
    return (SYSCTRL_GPIO1_IN & POWER_KEY_BIT) == 0;
}

/* Both speaker amplifiers, active high. See docs/soc/audio.md. */
void speaker_amp(uint32_t on)
{
    if (on)
        SYSCTRL_GPIO3_OUT |= AMP_ENABLE_BIT;
    else
        SYSCTRL_GPIO3_OUT &= ~AMP_ENABLE_BIT;
    SYSCTRL_GPIO3_DIR &= ~AMP_ENABLE_BIT;
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

/* Reload SP, because our call frames moved it, and unmask IRQ and FIQ. EBOOT
 * expects this state. */
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
