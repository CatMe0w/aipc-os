/*
 * DOOM key map over the shared CH374 keyboard driver. The driver reports raw
 * HID usage IDs, and this file turns them into doom keys.
 *
 * More than one usage can give the same doom key. Left control and right
 * control are both KEY_FIRE. Each doom key therefore has a reference count.
 * Without it, a release of one of two held modifiers would report the doom key
 * as released while the other one is still down.
 */

#include <stdint.h>
#include <string.h>

#include "../doomgeneric/doomgeneric/doomkeys.h"

#include "ch374.h"
#include "ch374_keyboard.h"

#define KEY_QUEUE_SIZE 64

static uint16_t s_queue[KEY_QUEUE_SIZE];
static unsigned int s_read_idx;
static unsigned int s_write_idx;
static uint8_t s_refcount[256];

static unsigned char modifier_to_doom_key(unsigned int bit)
{
    switch (bit) {
    case 0: return KEY_FIRE;        /* left control */
    case 1: return KEY_RSHIFT;      /* left shift */
    case 2: return KEY_LALT;        /* left alt */
    case 4: return KEY_FIRE;        /* right control */
    case 5: return KEY_RSHIFT;      /* right shift */
    case 6: return KEY_RALT;        /* right alt */
    default:
        return 0;
    }
}

static unsigned char usage_to_doom_key(uint8_t usage)
{
    if (usage >= CH374_USAGE_MODIFIER_BASE && usage <= CH374_USAGE_MODIFIER_BASE + 7u)
        return modifier_to_doom_key(usage - CH374_USAGE_MODIFIER_BASE);

    if (usage >= 0x04u && usage <= 0x1Du)
        return (unsigned char)('a' + (usage - 0x04u));

    if (usage >= 0x1Eu && usage <= 0x26u)
        return (unsigned char)('1' + (usage - 0x1Eu));

    switch (usage) {
    case 0x27: return '0';
    case 0x28: return KEY_ENTER;
    case 0x29: return KEY_ESCAPE;
    case 0x2A: return KEY_BACKSPACE;
    case 0x2B: return KEY_TAB;
    case 0x2C: return KEY_USE;
    case 0x2D: return KEY_MINUS;
    case 0x2E: return KEY_EQUALS;
    case 0x2F: return '[';
    case 0x30: return ']';
    case 0x31: return '\\';
    case 0x33: return ';';
    case 0x34: return '\'';
    case 0x35: return '`';
    case 0x36: return ',';
    case 0x37: return '.';
    case 0x38: return '/';
    case 0x39: return KEY_CAPSLOCK;
    case 0x3A: return KEY_F1;
    case 0x3B: return KEY_F2;
    case 0x3C: return KEY_F3;
    case 0x3D: return KEY_F4;
    case 0x3E: return KEY_F5;
    case 0x3F: return KEY_F6;
    case 0x40: return KEY_F7;
    case 0x41: return KEY_F8;
    case 0x42: return KEY_F9;
    case 0x43: return KEY_F10;
    case 0x44: return KEY_F11;
    case 0x45: return KEY_F12;
    case 0x49: return KEY_INS;
    case 0x4A: return KEY_HOME;
    case 0x4B: return KEY_PGUP;
    case 0x4C: return KEY_DEL;
    case 0x4D: return KEY_END;
    case 0x4E: return KEY_PGDN;
    case 0x4F: return KEY_RIGHTARROW;
    case 0x50: return KEY_LEFTARROW;
    case 0x51: return KEY_DOWNARROW;
    case 0x52: return KEY_UPARROW;
    default:
        return 0;
    }
}

static void queue_push(int pressed, unsigned char doom_key)
{
    s_queue[s_write_idx] = (uint16_t)(((pressed != 0) << 8) | doom_key);
    s_write_idx = (s_write_idx + 1u) % KEY_QUEUE_SIZE;
    if (s_write_idx == s_read_idx)
        s_read_idx = (s_read_idx + 1u) % KEY_QUEUE_SIZE;
}

static void emit(int pressed, unsigned char doom_key)
{
    if (doom_key == 0)
        return;

    if (pressed) {
        if (s_refcount[doom_key] == 0)
            queue_push(1, doom_key);
        if (s_refcount[doom_key] != 0xFFu)
            s_refcount[doom_key]++;
    } else {
        if (s_refcount[doom_key] == 0)
            return;
        s_refcount[doom_key]--;
        if (s_refcount[doom_key] == 0)
            queue_push(0, doom_key);
    }
}

static void drain(void)
{
    uint8_t usage;
    int pressed;

    while (ch374_kbd_pop(&usage, &pressed))
        emit(pressed, usage_to_doom_key(usage));
}

void aipc_keyboard_init(void)
{
    memset(s_queue, 0, sizeof(s_queue));
    memset(s_refcount, 0, sizeof(s_refcount));
    s_read_idx = 0;
    s_write_idx = 0;

    ch374_kbd_init();
    drain();
}

void aipc_keyboard_poll(void)
{
    ch374_kbd_poll();
    drain();
}

int aipc_keyboard_get_event(int *pressed, unsigned char *key)
{
    if (s_read_idx == s_write_idx)
        aipc_keyboard_poll();

    if (s_read_idx == s_write_idx)
        return 0;

    uint16_t packed = s_queue[s_read_idx];

    s_read_idx = (s_read_idx + 1u) % KEY_QUEUE_SIZE;
    *pressed = (packed >> 8) & 1u;
    *key = (unsigned char)(packed & 0xFFu);
    return 1;
}
