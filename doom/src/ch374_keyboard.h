#ifndef AIPC_CH374_KEYBOARD_H
#define AIPC_CH374_KEYBOARD_H

void aipc_keyboard_init(void);
void aipc_keyboard_poll(void);
int aipc_keyboard_get_event(int *pressed, unsigned char *key);

#endif
