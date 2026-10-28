#include "card.h"
#include "ff.h"
#include "log.h"
#include "sd.h"
#include "storage.h"
#include "timer.h"

/* Contact bounce and card power-up. Not measured. */
#define SETTLE_MS       300u
#define MOUNT_TRIES     3
#define MOUNT_RETRY_MS  200u

static enum card_state state;
static uint32_t settle_end;

static void mount(void)
{
    int rc = 0;

    for (int i = 0; i < MOUNT_TRIES; i++) {
        if (i)
            timer_delay_ms(MOUNT_RETRY_MS);
        rc = storage_mount();
        if (!rc) {
            state = CARD_READY;
            return;
        }
    }
    log_puts("card: mount");
    log_rc(rc);
    state = CARD_ERROR;
}

int card_init(void)
{
    enum card_state was = state;

    if (sd_card_present())
        mount();
    else
        state = CARD_NONE;
    return state != was;
}

int card_poll(void)
{
    int present = sd_card_present();

    if (!present) {
        if (state == CARD_NONE)
            return 0;
        f_unmount("");
        state = CARD_NONE;
        return 1;
    }
    switch (state) {
    case CARD_NONE:
        state = CARD_SETTLING;
        settle_end = timer_ms() + SETTLE_MS;
        return 0;
    case CARD_SETTLING:
        if ((int32_t)(settle_end - timer_ms()) > 0)
            return 0;
        mount();
        return 1;
    default:
        return 0;
    }
}

enum card_state card_state(void)
{
    return state;
}
