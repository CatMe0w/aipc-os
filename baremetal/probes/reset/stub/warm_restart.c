/*
 * Warm restart through the bootrom.
 *
 * The USB shutdown is what makes this work. A bare jump succeeds less than one
 * time in ten, because the block stays live and writes L2 SRAM that the
 * bootrom uses for its NAND read buffer and its stack.
 *
 * The reset vector sets its own CPSR and SP, so no register needs a restore.
 *
 * The stage colors are the only output channel left once the target hangs.
 * The LCD keeps scanning the framebuffer out of DRAM with no CPU help, so the
 * last color on the panel names the last stage reached.
 */

#include <stdint.h>

#define REG8(a)   (*(volatile uint8_t *)(uintptr_t)(a))
#define REG32(a)  (*(volatile uint32_t *)(uintptr_t)(a))

#define SYSCTRL              0x08000000u
#define INT_MASK_IRQ         (SYSCTRL + 0x34u)
#define INT_MASK_FIQ         (SYSCTRL + 0x38u)
#define MULFUN_CON1          (SYSCTRL + 0x58u)

/* The bootrom clears these three to disable the USB block, then writes 6 to
 * enable it. See docs/bootrom/memory-map.md. */
#define MULFUN_USB_SEL       0x7u

#define USB                  0x70000000u
#define USB_INTRTX1E         (USB + 0x06u)
#define USB_INTRRX1E         (USB + 0x08u)
#define USB_INTRUSBE         (USB + 0x0Bu)

#define BOOTROM_RESET_VECTOR 0x00000000u

#define FB_ADDR              0x33B00000u
#define FB_PIXELS            (800u * 480u)

#define RED                  0xF800u
#define YELLOW               0xFFE0u
#define GREEN                0x07E0u

static void mark(uint16_t color)
{
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)FB_ADDR;
    uint32_t i;

    for (i = 0; i < FB_PIXELS; i++)
        fb[i] = color;
}

void stub_main(void)
{
    mark(RED);

    /* The bootrom writes CPSR = 0x13 at entry, which unmasks IRQ and FIQ. The
     * IRQ vector forwards to a DDR word that no longer holds a handler. */
    REG32(INT_MASK_IRQ) = 0;
    REG32(INT_MASK_FIQ) = 0;
    mark(YELLOW);

    /* This part has no SOFTCONN, so the pull-up stays and the host still sees
     * a device. Deselecting the block is what stops it from writing L2. */
    REG8(USB_INTRTX1E) = 0;
    REG8(USB_INTRRX1E) = 0;
    REG8(USB_INTRUSBE) = 0;
    REG32(MULFUN_CON1) &= ~MULFUN_USB_SEL;
    mark(GREEN);

    __asm__ volatile ("mov pc, %0" : : "r"(BOOTROM_RESET_VECTOR));
    __builtin_unreachable();
}
