#include <string.h>
#include "boot.h"
#include "card.h"
#include "cfw_abi.h"
#include "ff.h"
#include "kbd.h"
#include "lcd.h"
#include "nandbackup.h"
#include "nboottool.h"
#include "power.h"
#include "settings.h"
#include "sysinfo.h"
#include "ui_priv.h"

#define TIMEOUT_MAX_S  99u

/* Writes settings.ini, or shows why the write failed. A change that is not
 * written stays in memory until a card goes in, and the settings on that
 * card replace it. */
static void save(const char *label)
{
    if (card_state() == CARD_READY && settings_save())
        modal_error(label, "Failed to save settings",
                    "settings.ini could not be written to the SD card.");
}

/* WinCE CFW */

/* The component of each item, in the order that cfw_build() adds them */
static const struct cfw_component_info *cfw_items[32];

static void cfw_section(uint32_t flag, const char *title, int *n)
{
    int first = 1;

    for (int i = 0; i < cfw_component_count(); i++) {
        const struct cfw_component_info *c = cfw_component(i);

        if (!(c->flags & flag))
            continue;
        if (first)
            ui_section(title);
        first = 0;
        if (!ui_item(c->name,
                     cfw_component_on(c) ? META_TOGGLE_ON : META_TOGGLE_OFF,
                     NULL, c->description, NULL))
            return;
        cfw_items[(*n)++] = c;
    }
}

static void cfw_build(void)
{
    int n = 0;

    cfw_section(CFW_FIX, "Fixes", &n);
    cfw_section(CFW_EXTRA, "Extras", &n);
}

static void cfw_enter(int item)
{
    const struct cfw_component_info *c = cfw_items[item];

    settings_set_cfw(c->id, !cfw_component_on(c));
    save(c->name);
    ui_rebuild();
}

static void cfw_hints(int item, const char **enter, const char **side)
{
    *enter = cfw_component_on(cfw_items[item]) ? "Turn off" : "Turn on";
    *side = NULL;
}

const struct page page_cfw = {
    .name = "WinCE CFW",
    .icon = ICON_CFW,
    .build = cfw_build,
    .enter = cfw_enter,
    .hints = cfw_hints,
};

/* Tools */

struct tool {
    const char *title;
    const char *help;
    const char *warn;
};

static const struct tool tools_backup[] = {
    { "Back up NAND", "Save the entire NAND to /backup on the SD card.", NULL },
};

/* The backups in /backup. Each one is an item of the Tools page, after
 * "Back up NAND". */
#define BACKUPS_MAX 8
static char backups[BACKUPS_MAX][64];
static char backup_titles[BACKUPS_MAX][72];
static int backup_count;

static void backups_scan(void)
{
    static const char suffix[] = ".img.lz4";
    DIR dir;
    FILINFO info;

    backup_count = 0;
    if (f_opendir(&dir, "/backup") != FR_OK)
        return;
    while (backup_count < BACKUPS_MAX && f_readdir(&dir, &info) == FR_OK &&
           info.fname[0]) {
        size_t n = strlen(info.fname);

        if ((info.fattrib & AM_DIR) || n < sizeof(suffix) ||
            n >= sizeof(backups[0]) ||
            strcmp(info.fname + n - (sizeof(suffix) - 1), suffix))
            continue;
        strcpy(backups[backup_count++], info.fname);
    }
    f_closedir(&dir);
}

static const struct tool tools_install[] = {
    { "Install openNBOOT",
      "Install openNBOOT to boot aipc-boot from the SD card at power-on.",
      "NAND block 0 will be overwritten." },
};

static const struct tool tools_installed[] = {
    { "Reinstall openNBOOT",
      "Replace the installed openNBOOT with the version in this release.",
      "NAND block 0 will be overwritten." },
    { "Remove openNBOOT", "Restore the original bootloader from /backup.",
      "NAND block 0 will be overwritten." },
};

static void tool_items(const struct tool *t, int n)
{
    for (int i = 0; i < n; i++)
        if (!ui_item(t[i].title, META_NONE, NULL, t[i].help, t[i].warn))
            return;
}

static void tools_build(void)
{
    /* The tools read or write files in /backup on the card. */
    if (card_state() != CARD_READY) {
        ui_note("Insert an SD card to use the tools.");
        return;
    }
    ui_section("NAND backup and restore");
    tool_items(tools_backup, 1);
    backups_scan();
    for (int i = 0; i < backup_count; i++) {
        lv_snprintf(backup_titles[i], sizeof(backup_titles[i]), "Restore %s",
                    backups[i]);
        if (!ui_item(backup_titles[i], META_NONE, NULL,
                     "Erase the NAND and write this backup to it.",
                     "All data on the NAND will be erased."))
            return;
    }
    ui_section("NAND bootloader");
    if (sysinfo_nboot() == NBOOT_OPENNBOOT)
        tool_items(tools_installed, 2);
    else
        tool_items(tools_install, 1);
}

/* Esc cancels. Returns nonzero then. */
static int backup_progress(uint32_t permille)
{
    uint8_t key;
    int pressed, cancel = 0;

    kbd_poll();
    while (kbd_pop(&key, &pressed))
        cancel |= pressed && key == KBD_ESC;
    modal_progress(permille);
    lv_timer_handler();
    return cancel;
}

static int asked;

static void ask_done(int ok)
{
    asked = ok;
}

/* Shows a modal and waits for the answer. Returns 1 for the ok button. */
static int ask(struct modal *m)
{
    m->done = ask_done;
    asked = 0;
    modal_open(m);
    while (modal_is_open()) {
        uint8_t key;
        int pressed;

        kbd_poll();
        while (kbd_pop(&key, &pressed))
            if (pressed && modal_is_open())
                modal_key(key);
        /* No frame without the modal. The caller can open the next one, and
         * the page under them must not show in between. */
        if (modal_is_open())
            lv_timer_handler();
    }
    return asked;
}

static void backup_nand(void)
{
    static const char label[] = "Back up NAND";
    static char name[64], body[200], space[24];
    struct modal confirm = {
        .label = label,
        .title = "Back up the NAND?",
        .body = body,
        .cancel = "Cancel",
        .ok = "Back up",
    };
    struct modal progress = {
        .label = label,
        .title = "Backing up the NAND",
        .body = "Hold Esc to cancel.",
        .progress = 1,
    };
    const struct ui_card *card = ui_card();
    int rc;

    if (card->space_ok) {
        ui_size_text(space, sizeof(space), card->free_mb);
        lv_snprintf(body, sizeof(body),
                    "The entire NAND will be saved to /backup on the SD card.\n"
                    "Free space on the SD card: "
                    "%s.", space);
    } else {
        lv_snprintf(body, sizeof(body),
                    "The entire NAND will be saved to /backup on the SD card.");
    }
    if (!ask(&confirm))
        return;

    modal_open(&progress);
    lv_timer_handler();
    rc = nand_backup(backup_progress, name, sizeof(name));
    modal_close();
    ui_card_changed();

    switch (rc) {
    case NANDBACKUP_OK:
        lv_snprintf(body, sizeof(body), "Saved as /backup/%s.", name);
        modal_error(label, "Backup complete", body);
        break;
    case NANDBACKUP_CANCELLED:
        break;
    case NANDBACKUP_EEXISTS:
        lv_snprintf(body, sizeof(body),
                    "An identical backup is already on the SD card: %s.", name);
        modal_error(label, "Backup already exists", body);
        break;
    case NANDBACKUP_ENAND:
        modal_error(label, "NAND read error",
                    "The NAND could not be read. No backup was created.");
        break;
    default:
        modal_error(label, "SD card write error",
                    "The backup could not be written. The SD card may be "
                    "full.");
        break;
    }
}

static int warn(const char *label, const char *title, const char *body)
{
    struct modal m = {
        .label = label,
        .title = title,
        .body = body,
        .cancel = "Cancel",
        .ok = "Restore anyway",
        .danger = 1,
    };

    return ask(&m);
}

static int no_cancel_progress(uint32_t permille)
{
    modal_progress(permille);
    lv_timer_handler();
    return 0;
}

static void restore_failed(const char *label, int rc, int written)
{
    static const char partial[] =
        "The NAND is only partially written. Run the restore again.";

    switch (rc) {
    case NANDBACKUP_CANCELLED:
        break;
    case NANDBACKUP_EFORMAT:
        modal_error(label, "Invalid backup file",
                    written ? partial
                            : "The file is not a NAND backup, or its size "
                              "does not match this NAND.");
        break;
    case NANDBACKUP_ENAND:
        modal_error(label, written ? "NAND write error" : "NAND read error",
                    written ? partial
                            : "The NAND could not be read. The NAND was not "
                              "modified.");
        break;
    default:
        modal_error(label, "SD card read error",
                    written ? partial
                            : "The backup could not be read. The NAND was "
                              "not modified.");
        break;
    }
}

static void restore_nand(const char *name)
{
    static const char label[] = "Restore NAND";
    static char path[80], body[240];
    struct nand_restore_check check;
    struct modal confirm = {
        .label = label,
        .title = "Restore this backup?",
        .body = body,
        .cancel = "Cancel",
        .ok = "Restore",
        .danger = 1,
    };
    struct modal progress = {
        .label = label,
        .title = "Verifying the backup",
        .body = "Hold Esc to cancel.",
        .progress = 1,
    };
    int rc;

    lv_snprintf(body, sizeof(body),
                "All data on the NAND will be erased and replaced with %s. ",
                name);
    if (!ask(&confirm))
        return;

    lv_snprintf(path, sizeof(path), "/backup/%s", name);
    modal_open(&progress);
    lv_timer_handler();
    rc = nand_restore_check(path, backup_progress, &check);
    modal_close();
    if (rc) {
        restore_failed(label, rc, 0);
        return;
    }

    if (!check.checksum_ok &&
        !warn(label, "Checksum mismatch",
              "The backup does not match its checksum. The file is corrupted "
              "or was modified. Restore anyway?"))
        return;
    if (!check.name_ok &&
        !warn(label, "File name mismatch",
              "The file name does not match the checksum of the backup. The "
              "file was renamed or modified. Restore anyway?"))
        return;
    if (check.lost) {
        lv_snprintf(body, sizeof(body),
                    "The backup contains data in %u blocks that are marked "
                    "bad on this device. These part of the data cannot be restored. "
                    "Restore anyway?", (unsigned)check.lost);
        if (!warn(label, "Bad blocks", body))
            return;
    }

    progress.title = "Restoring the NAND";
    progress.body = "Do not power off the device.";
    modal_open(&progress);
    lv_timer_handler();
    rc = nand_restore_write(path, no_cancel_progress);
    modal_close();
    if (rc) {
        restore_failed(label, rc, 1);
        return;
    }
    modal_error(label, "Restore complete",
                "The backup was written to the NAND.");
}

static const char not_modified[] = " The NAND was not modified.";

static int nboot_failed(const char *label, int rc)
{
    static char body[280];
    const char *title, *text;

    switch (rc) {
    case NBOOTTOOL_OK:
        return 0;
    case NBOOTTOOL_ENAND:
        title = "NAND read error";
        text = "NAND block 0 could not be read.";
        break;
    case NBOOTTOOL_EBLOCK0:
        title = "Unsupported bootloader";
        text = "NAND block 0 does not contain a valid boot header. A "
               "bootloader cannot be written without this header.";
        break;
    case NBOOTTOOL_ENOBACKUP:
        title = "Backup not found";
        text = "/backup on the SD card contains no backup of the original "
               "bootloader of this device.";
        break;
    case NBOOTTOOL_EDAMAGED:
        title = "Backup damaged";
        text = "A backup in /backup on the SD card is damaged: its contents "
               "do not match the checksum in its file name. No other backup "
               "matches this device.";
        break;
    case NBOOTTOOL_EAMBIGUOUS:
        title = "Multiple backups found";
        text = "/backup on the SD card contains more than one backup that "
               "matches this device, and their contents differ. Keep only "
               "the backup of this device.";
        break;
    case NBOOTTOOL_EWRITE:
        modal_error(label, "NAND write error",
                    "The write to NAND block 0 failed. The previous "
                    "bootloader was written back, and the device starts as "
                    "before.");
        return rc;
    case NBOOTTOOL_EROLLBACK:
        modal_error(label, "NAND write error",
                    "The write to NAND block 0 failed, and the previous "
                    "bootloader could not be written back. The device cannot "
                    "start from NAND. To recover, enter USB boot mode and "
                    "run \"opennboot restore\" on a host.");
        return rc;
    default:
        title = "SD card error";
        text = "The SD card could not be read or written.";
        break;
    }
    lv_snprintf(body, sizeof(body), "%s%s", text, not_modified);
    modal_error(label, title, body);
    return rc;
}

static void install_nboot(int reinstall)
{
    const char *label = reinstall ? "Reinstall openNBOOT" : "Install openNBOOT";
    static char name[64], body[200];
    struct modal confirm = {
        .label = label,
        .title = reinstall ? "Reinstall openNBOOT?" : "Install openNBOOT?",
        .body = reinstall
            ? "NAND block 0 will be erased, and openNBOOT will be written "
              "to it."
            : "NAND block 0 will be erased, and openNBOOT will be written "
              "to it. The current bootloader will be saved to /backup on "
              "the SD card first.",
        .cancel = "Cancel",
        .ok = reinstall ? "Reinstall" : "Install",
        .danger = 1,
    };

    int rc;

    if (nboot_failed(label, nboot_prepare_install()) || !ask(&confirm))
        return;
    rc = reinstall ? 0 : nboot_backup(name, sizeof(name));
    if (rc == 0)
        rc = nboot_write();
    /* It closes any modal, so it comes before the result. */
    ui_card_changed();
    if (nboot_failed(label, rc))
        return;
    if (reinstall) {
        modal_error(label, "Reinstallation complete",
                    "openNBOOT was written to NAND block 0.");
        return;
    }
    lv_snprintf(body, sizeof(body),
                "openNBOOT was written to NAND block 0. The previous "
                "bootloader was saved as /backup/%s. Keep this file.", name);
    modal_error(label, "Installation complete", body);
}

static void remove_nboot(void)
{
    static const char label[] = "Remove openNBOOT";
    static char name[64], body[200];
    struct modal confirm = {
        .label = label,
        .title = "Remove openNBOOT?",
        .body = body,
        .cancel = "Cancel",
        .ok = "Remove",
        .danger = 1,
    };
    int rc;

    if (nboot_failed(label, nboot_prepare_remove(name, sizeof(name))))
        return;
    lv_snprintf(body, sizeof(body),
                "NAND block 0 will be erased, and the original bootloader "
                "will be written to it from /backup/%s.", name);
    if (!ask(&confirm))
        return;
    rc = nboot_write();
    ui_card_changed();
    if (nboot_failed(label, rc))
        return;
    modal_error(label, "Removal complete",
                "The original bootloader was written to NAND block 0.");
}

static void tools_enter(int item)
{

    if (item == 0) {
        backup_nand();
        ui_rebuild();
    } else if (item <= backup_count) {
        restore_nand(backups[item - 1]);
    } else if (sysinfo_nboot() != NBOOT_OPENNBOOT) {
        install_nboot(0);
    } else if (item == backup_count + 1) {
        install_nboot(1);
    } else {
        remove_nboot();
    }
}

static void tools_hints(int item, const char **enter, const char **side)
{
    if (item == 0)
        *enter = "Back up";
    else if (item <= backup_count)
        *enter = "Restore";
    else if (sysinfo_nboot() != NBOOT_OPENNBOOT)
        *enter = "Install";
    else
        *enter = item == backup_count + 1 ? "Reinstall" : "Remove";
    *side = NULL;
}

const struct page page_tools = {
    .name = "Tools",
    .icon = ICON_TOOLS,
    .build = tools_build,
    .enter = tools_enter,
    .hints = tools_hints,
};

/* Settings */

enum {
    OPT_DEFAULT,
    OPT_TIMEOUT,
    OPT_BACKLIGHT,
    OPT_THEME,
    OPT_RESET,
};

#define BACKLIGHT_STEP  10u

/* Without a card, only the brightness and the theme show: the default
 * entry and the autoboot time need the card. */
static int option_of_item(int item)
{
    return card_state() == CARD_READY ? item : OPT_BACKLIGHT + item;
}

static void settings_build(void)
{
    char t[16], b[16];

    lv_snprintf(t, sizeof(t), "%u s", (unsigned)settings_timeout());
    lv_snprintf(b, sizeof(b), "%u%%", (unsigned)settings_backlight());
    if (card_state() == CARD_READY) {
        ui_choice("Default entry", launch_title(launch_default()));
        ui_choice("Auto boot after", t);
    }
    ui_choice("Brightness", b);
    ui_choice("Theme", settings_dark() ? "Dark" : "Light");
    if (card_state() == CARD_READY)
        ui_item("Reset settings", META_NONE, NULL, NULL, NULL);
}

static int settings_side(int item, int dir)
{
    item = option_of_item(item);
    if (item == OPT_DEFAULT) {
        int n = launch_count();

        settings_set_default(launch_title((launch_default() + dir + n) % n));
    } else if (item == OPT_TIMEOUT) {
        uint32_t s = settings_timeout();

        /* A larger value from settings.ini leaves the range. */
        if (s > TIMEOUT_MAX_S)
            s = dir > 0 ? 1 : TIMEOUT_MAX_S;
        else if (dir > 0)
            s = s == TIMEOUT_MAX_S ? 1 : s + 1;
        else
            s = s == 1 ? TIMEOUT_MAX_S : s - 1;
        settings_set_timeout(s);
    } else if (item == OPT_BACKLIGHT) {
        uint32_t b = settings_backlight();

        if (dir > 0)
            b = b >= 100u ? BACKLIGHT_MIN : b + BACKLIGHT_STEP;
        else
            b = b <= BACKLIGHT_MIN ? 100u : b - BACKLIGHT_STEP;
        settings_set_backlight(b > 100u ? 100u : b);
    } else if (item == OPT_THEME) {
        settings_set_dark(!settings_dark());
    } else {
        return 0;
    }
    /* A new theme closes any open modal, so it comes before the save error. */
    lcd_backlight(settings_backlight());
    ui_apply_theme();
    save("Settings");
    return 1;
}

static void reset_done(int ok)
{
    if (!ok)
        return;
    settings_reset();
    lcd_backlight(settings_backlight());
    ui_apply_theme();
    save("Reset settings");
}

static void settings_enter(int item)
{
    static const struct modal m = {
        .label = "Settings",
        .title = "Reset settings?",
        .body = "All boot menu and WinCE CFW settings will be reset to their "
                "defaults.",
        .cancel = "Cancel",
        .ok = "Reset",
        .done = reset_done,
    };

    if (option_of_item(item) == OPT_RESET)
        modal_open(&m);
}

static void settings_hints(int item, const char **enter, const char **side)
{
    item = option_of_item(item);
    *enter = item == OPT_RESET ? "Reset" : NULL;
    *side = item == OPT_RESET ? NULL : "Change";
}

const struct page page_settings = {
    .name = "Settings",
    .icon = ICON_SETTINGS,
    .build = settings_build,
    .enter = settings_enter,
    .side = settings_side,
    .hints = settings_hints,
};

/* Info. The order follows the boot chain, then the hardware. */

static void info_build(void)
{
    const struct ui_card *c = ui_card();
    const char *fw = sysinfo_fw_version();
    char text[48], total[16], free[16];

    ui_section("Boot chain");
    ui_key_value("NAND bootloader", sysinfo_nboot_name());
    ui_key_value("aipc-boot", aipc_boot_version);
    lv_snprintf(text, sizeof(text), "%s%s", fw ? "v" : "", fw ? fw : "Unknown");
    ui_key_value("Firmware", text);
    ui_section("Hardware");
    if (card_state() != CARD_READY) {
        strcpy(text, "No SD card");
    } else if (!c->space_ok) {
        strcpy(text, "Unknown");
    } else {
        ui_size_text(total, sizeof(total), c->total_mb);
        ui_size_text(free, sizeof(free), c->free_mb);
        lv_snprintf(text, sizeof(text), "%s, %s, %s free", c->type, total,
                    free);
    }
    ui_key_value("Boot partition", text);
    ui_battery_text(text, sizeof(text));
    ui_key_value("Battery", text);
}

const struct page page_info = {
    .name = "Info",
    .icon = ICON_INFO,
    .build = info_build,
};

/* Power */

static void power_build(void)
{
    ui_item("Power off", META_NONE, NULL, NULL, NULL);
    ui_item("Restart", META_NONE, NULL, NULL, NULL);
}

static void power_enter(int item)
{
    if (item == 0)
        power_off();
    power_restart();
}

static void power_hints(int item, const char **enter, const char **side)
{
    *enter = item == 0 ? "Power off" : "Restart";
    *side = NULL;
}

const struct page page_power = {
    .name = "Power",
    .icon = ICON_POWER,
    .build = power_build,
    .enter = power_enter,
    .hints = power_hints,
};
