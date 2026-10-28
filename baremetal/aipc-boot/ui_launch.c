#include <string.h>
#include "boot.h"
#include "card.h"
#include "entries.h"
#include "kbd.h"
#include "payload.h"
#include "settings.h"
#include "soc.h"
#include "storage.h"
#include "ui_priv.h"

enum kind {
    KIND_LINUX,
    KIND_WINCE_CFW,
    KIND_WINCE,
};

struct launch_item {
    enum kind kind;
    const struct entry *entry;
    const char *title;
    const char *help;
};

static struct launch_item launch[ENTRIES_MAX + 2];
static int count;

void launch_reload(void)
{
    count = 0;
    for (int i = 0; i < entry_count; i++)
        launch[count++] = (struct launch_item){
            KIND_LINUX, &entries[i], entries[i].title,
            entries[i].description };
    launch[count++] = (struct launch_item){
        KIND_WINCE_CFW, NULL, "Windows CE (CFW)",
        "Boots from NAND, with the custom firmware." };
    launch[count++] = (struct launch_item){
        KIND_WINCE, NULL, "Windows CE (unmodified)",
        "Boots from NAND, without any modifications." };
}

int launch_count(void)
{
    return count;
}

const char *launch_title(int i)
{
    return launch[i].title;
}

int launch_find(const char *title)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(launch[i].title, title))
            return i;
    return -1;
}

int launch_default(void)
{
    int i = launch_find(settings_default());

    return i < 0 ? 0 : i;
}

static void load_failed(const char *label, int rc, const char *path)
{
    static char body[160];
    const char *title;

    lv_snprintf(body, sizeof(body), "%s", path);
    switch (rc) {
    case STORAGE_ENOTFOUND:
        title = "File not found";
        break;
    case STORAGE_ESIZE:
        title = "File is empty or too large";
        break;
    case STORAGE_EIO:
        title = "SD card read error";
        break;
    case PAYLOAD_ENOTELF:
        title = "Not an ARM executable";
        lv_snprintf(body, sizeof(body), "%s is not a 32-bit little-endian "
                    "ARM ELF executable.", path);
        break;
    case PAYLOAD_ERANGE:
        title = "Load address not allowed";
        lv_snprintf(body, sizeof(body), "A segment of %s is outside DDR, or "
                    "covers aipc-boot or a framebuffer.", path);
        break;
    default:
        title = "Boot failed";
        lv_snprintf(body, sizeof(body), "%s: error %d.", path, rc);
        break;
    }
    ui_light();
    modal_error(label, title, body);
}

static int need_card(const char *label)
{
    if (card_state() == CARD_READY)
        return 0;
    modal_error(label, "No SD card", "Insert the SD card and try again.");
    return 1;
}

static void boot_linux(const struct launch_item *it)
{
    const struct entry *e = it->entry;
    int rc;

    if (need_card(it->title))
        return;
    if (e->error[0]) {
        modal_error(it->title, "Entry not valid", e->error);
        return;
    }
    ui_dark();
    rc = linux_load(e);
    if (rc) {
        load_failed(it->title, rc, e->kernel);
        return;
    }
    linux_enter(e);
}

/* On USB power alone, the battery input reads about 4.1 V to 5.1 V. */
#define BATTERY_PRESENT_MV  6000

/* Without the power_hold component, only the power key keeps the device on
 * while EBOOT starts. On USB power alone, the rail stays up without it. */
static int need_power_key(int cfw)
{
    int mv = ui_battery_mv();

    /* Without a reading, ask. A wrong prompt costs a key press, and a
     * missing one turns the device off. */
    return !wince_power_held(cfw) && (mv < 0 || mv >= BATTERY_PRESENT_MV);
}

/* Returns 1 when the key is down, 0 when the user cancels. */
static int wait_power_key(const char *label)
{
    struct modal m = {
        .label = label,
        .title = "Hold the power key",
        .body = "Hold the power key until the Windows CE boot screen "
                "appears.",
        .ok = "Cancel",
    };

    modal_open(&m);
    for (;;) {
        uint8_t key;
        int pressed;

        if (power_key_down())
            return 1;
        kbd_poll();
        while (kbd_pop(&key, &pressed)) {
            if (pressed && (key == KBD_ESC || key == KBD_ENTER)) {
                modal_close();
                return 0;
            }
        }
        lv_timer_handler();
    }
}

/* The prompt comes before the fade, so the menu does not show again
 * between the two. */
static void boot_wince(const struct launch_item *it)
{
    int cfw = it->kind == KIND_WINCE_CFW;
    int rc;

    if (need_power_key(cfw) && !wait_power_key(it->title))
        return;
    ui_dark();
    rc = wince_prepare(cfw);
    if (rc) {
        /* wince_prepare() gives the pads to the NAND controller. */
        if (card_init())
            ui_card_changed();
        /* The prompt told the user to hold the power key until WinCE
         * starts, so the key can still be down. */
        ui_power_key_rearm();
        ui_light();
        if (rc == BOOT_ECFW) {
            modal_error(it->title, "CFW could not patch EBOOT", cfw_error());
        } else {
            static char body[64];

            lv_snprintf(body, sizeof(body),
                        "EBOOT could not be read from NAND. Error %d.", rc);
            modal_error(it->title, "NAND read error", body);
        }
        return;
    }
    wince_enter();
}

void launch_boot(int i)
{
    const struct launch_item *it = &launch[i];

    if (it->kind == KIND_LINUX)
        boot_linux(it);
    else
        boot_wince(it);
}

static void launch_build(void)
{
    int def = launch_default();

    for (int i = 0; i < count; i++) {
        /* Without a card, settings.ini cannot keep a default, and autoboot
         * does not run. */
        int is_def = i == def && card_state() == CARD_READY;

        if (!ui_item(launch[i].title, is_def ? META_TEXT : META_NONE,
                     "Default", launch[i].help, NULL))
            break;
    }
}

static void launch_hints(int i, const char **enter, const char **side)
{
    (void)i;
    *enter = "Boot";
    *side = NULL;
}

const struct page page_launch = {
    .name = "Launch",
    .icon = ICON_LAUNCH,
    .build = launch_build,
    .enter = launch_boot,
    .hints = launch_hints,
};

/* Payloads */

static void payloads_build(void)
{
    if (card_state() != CARD_READY) {
        ui_note("Insert an SD card to see the payloads.");
        return;
    }
    if (!payload_count) {
        ui_note("There are no files in " PAYLOAD_DIR ".");
        return;
    }
    for (int i = 0; i < payload_count; i++)
        if (!ui_item(payload_names[i], META_NONE, NULL, NULL, NULL))
            break;
}

static void payloads_enter(int i)
{
    const char *name = payload_names[i];
    uint32_t entry;
    int rc;

    if (need_card(name))
        return;
    ui_dark();
    rc = payload_load(name, &entry);
    if (rc) {
        load_failed(name, rc, name);
        return;
    }
    payload_call(entry);
    ui_resume();
}

static void payloads_hints(int i, const char **enter, const char **side)
{
    (void)i;
    *enter = "Run";
    *side = NULL;
}

const struct page page_payloads = {
    .name = "Payloads",
    .icon = ICON_PAYLOADS,
    .build = payloads_build,
    .enter = payloads_enter,
    .hints = payloads_hints,
};
