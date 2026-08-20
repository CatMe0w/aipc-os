/*
 * Menu key map over the shared CH374 keyboard driver. The driver reports raw
 * HID usage IDs. Only the few that a menu needs pass through here.
 */

#include "kbd.h"

#include "ch374.h"

static uint8_t usage_to_key(uint8_t usage)
{
    switch (usage) {
    case 0x28: return KBD_ENTER;        /* Enter */
    case 0x58: return KBD_ENTER;        /* keypad Enter */
    case 0x2C: return KBD_ENTER;        /* Space */
    case 0x29: return KBD_ESC;
    case 0x4A: return KBD_HOME;
    case 0x4D: return KBD_END;
    case 0x4F: return KBD_RIGHT;
    case 0x50: return KBD_LEFT;
    case 0x51: return KBD_DOWN;
    case 0x52: return KBD_UP;
    default:   return 0;
    }
}

void kbd_init(void)
{
    ch374_kbd_init();
}

void kbd_poll(void)
{
    ch374_kbd_poll();
}

int kbd_ready(void)
{
    return ch374_kbd_ready();
}

int kbd_pop(uint8_t *key, int *pressed)
{
    uint8_t usage;

    /* Keys that the menu does not use still reach the queue. Skip them instead
     * of a report that the caller cannot act on. */
    while (ch374_kbd_pop(&usage, pressed)) {
        uint8_t mapped = usage_to_key(usage);

        if (mapped != 0) {
            *key = mapped;
            return 1;
        }
    }

    return 0;
}
