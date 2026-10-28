#pragma once

enum card_state {
    CARD_NONE,       /* the slot is empty */
    CARD_SETTLING,   /* a card went in, and the contacts settle */
    CARD_READY,      /* mounted */
    CARD_ERROR,      /* in the slot, but the mount failed */
};

/* Mounts the card that is in the slot at once, without the settle time. Call
 * it at start, and after anything that can have changed the SD controller or
 * taken the SD pads, for example a payload or a NAND read. Returns 1 when the
 * state changed. */
int card_init(void);

/* Call it from the main loop. It mounts a card that goes in and unmounts one
 * that comes out. Returns 1 when the state became NONE, READY or ERROR. */
int card_poll(void);

enum card_state card_state(void);
