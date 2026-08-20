#pragma once
#include <stdint.h>

/*
 * Internal keyboard: a USB HID device behind a CH374 USB host bridge on SPI.
 *
 * Each event is a raw HID usage ID, thus each image keeps its own key map.
 * A modifier key has its own usage ID, 0xE0 (left control) through 0xE7 (right
 * GUI), the numbering that the HID usage tables already give it.
 *
 * timer.c must run before ch374_kbd_init().
 */

#define CH374_USAGE_MODIFIER_BASE  0xE0u

void ch374_kbd_init(void);

/* Drains the CH374, and enumerates the keyboard again after it disconnects.
 * Cheap enough to call from a UI or game loop. */
void ch374_kbd_poll(void);

int ch374_kbd_ready(void);

/* Oldest queued event, 0 when there is nothing queued. */
int ch374_kbd_pop(uint8_t *usage, int *pressed);
