#pragma once
#include <stdint.h>

enum {
    KBD_UP = 1,
    KBD_DOWN,
    KBD_LEFT,
    KBD_RIGHT,
    KBD_ENTER,
    KBD_ESC,
    KBD_HOME,
    KBD_END,
};

void kbd_init(void);

/* Drains the CH374, and enumerates the keyboard again after it disconnects.
 * Cheap enough to call from the UI loop. */
void kbd_poll(void);

int kbd_ready(void);

/* Oldest queued event, 0 when there is nothing queued. */
int kbd_pop(uint8_t *key, int *pressed);
