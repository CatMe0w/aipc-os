#include <string.h>
#include "boot.h"
#include "card.h"
#include "cfw_abi.h"
#include "lcd.h"
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
        modal_error(label, "Could not save the setting",
                    "The SD card could not write settings.ini.");
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

/* Tools. Only the items exist so far. */

struct tool {
    const char *title;
    const char *help;
    const char *warn;
};

static const struct tool tools_backup[] = {
    { "Back up NAND", "Copies all of NAND to /backup on the SD card.", NULL },
    { "Restore NAND", "Writes a backup from /backup back to NAND.",
      "Erases all of NAND." },
};

static const struct tool tools_install[] = {
    { "Install openNBOOT", "Starts aipc-boot from the SD card at power on.",
      "Writes NAND block 0." },
};

static const struct tool tools_update[] = {
    { "Update openNBOOT", "Writes this release of openNBOOT.",
      "Writes NAND block 0." },
    { "Remove openNBOOT", "Puts back the stock bootloader from /backup.",
      "Writes NAND block 0." },
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
    ui_section("NAND backup");
    tool_items(tools_backup, 2);
    /* The tools do not work yet, so an unknown block 0 shows the same
     * placeholder as the stock bootloader. */
    ui_section("NAND bootloader");
    if (sysinfo_nboot() == NBOOT_OPENNBOOT)
        tool_items(tools_update, 2);
    else
        tool_items(tools_install, 1);
}

static void tools_enter(int item)
{
    (void)item;
    modal_error("Tools", "Not available",
                "This version of aipc-boot does not have the tools yet.");
}

static void tools_hints(int item, const char **enter, const char **side)
{
    (void)item;
    *enter = "Open";
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
        .body = "This resets the settings of the boot menu and of WinCE CFW.",
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
