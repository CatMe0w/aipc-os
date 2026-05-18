/*
 * sd_gpio_probe - check GPIO39(MCIO_CMD) and GPIO40(MCIO_CLK) state
 *
 * GPIO39 = bank1 pin7 -> GPIO2_DIR[7], GPIO2_OUT[7], GPIO2_IN[7]
 * GPIO40 = bank1 pin8 -> GPIO2_DIR[8], GPIO2_OUT[8], GPIO2_IN[8]
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYSCTRL_BASE 0x08000000u
#define SYSCTRL(off)  REG32(SYSCTRL_BASE + (off))

#define RESULT_BASE   0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

#define GPIO2_DIR     SYSCTRL(0x84)
#define GPIO2_OUT     SYSCTRL(0x88)
#define GPIO2_IN      SYSCTRL(0xC0)
#define GPIO2_AUX     SYSCTRL(0xA0)  /* bank1 auxiliary (pull-up/down/IE) */

void stub_main(void)
{
    for (uint32_t i = 0; i < 16u; i++) OUT[i] = 0;

    OUT[0] = 0x4750494Fu; /* "GPIO" */
    OUT[1] = 1u;
    OUT[2] = 0;

    /* CMD pin: GPIO39 = bank1 bit7 */
    OUT[3] = GPIO2_DIR;          /* GPIO2 direction */
    OUT[4] = GPIO2_OUT;          /* GPIO2 output data */
    OUT[5] = GPIO2_IN;           /* GPIO2 input data */
    OUT[6] = GPIO2_AUX;          /* GPIO2 aux config (pull-up/down) */

    /* Also check GPIO1 (bank0) for DATA pins GPIO30-37 */
    OUT[7]  = SYSCTRL(0x7C);     /* GPIO1 direction */
    OUT[8]  = SYSCTRL(0x80);     /* GPIO1 output */
    OUT[9]  = SYSCTRL(0xBC);     /* GPIO1 input */
    OUT[10] = SYSCTRL(0x9C);     /* GPIO1 aux config */

    /* sharepin state */
    OUT[11] = SYSCTRL(0x74);
    OUT[12] = SYSCTRL(0x78);
    OUT[13] = SYSCTRL(0x0C);
    OUT[14] = SYSCTRL(0x10);
    OUT[15] = 0;

    /*
     * Try setting pull-ups on GPIO39/40 and DATA pins.
     * For GPIO2 (bank1), AUX register at +0xA0:
     *   bit 7 = GPIO39, bit 8 = GPIO40
     * Set both to 1 to enable pull-up.
     */
    GPIO2_AUX |= (1u << 7) | (1u << 8);
    /* For GPIO1 (bank0), AUX register at +0x9C:
     *   DATA pins GPIO30-37 = bits 30-37
     *   (but +0x9C uses pin number offset within bank = 30-37)
     */
    SYSCTRL(0x9C) |= (0xFFu << 30);  /* all DATA pins bank0 */
}
