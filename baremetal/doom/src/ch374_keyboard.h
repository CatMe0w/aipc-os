#ifndef AIPC_CH374_KEYBOARD_H
#define AIPC_CH374_KEYBOARD_H

/* Doom keys from the internal keyboard. See lib/ch374.h for the driver. */

void aipc_keyboard_init(void);

/* Drains the driver queue and maps what it finds. */
void aipc_keyboard_poll(void);

/* Oldest queued event, 0 when there is nothing queued. Polls by itself when
 * the queue is empty, thus DG_GetKey() needs no separate poll. */
int aipc_keyboard_get_event(int *pressed, unsigned char *key);

#endif
