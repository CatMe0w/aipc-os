#pragma once
#include <stdint.h>

#define SETTINGS_PATH  "/aipc-boot/settings.ini"

/* Reads the file. A missing file, an unreadable file, and a missing key give
 * the defaults. */
void settings_load(void);

/* Forgets the file and returns to the defaults. */
void settings_reset(void);

/* The title of the boot entry to select first, or "" for the first entry. */
const char *settings_default(void);
void settings_set_default(const char *title);

/* Seconds before the default entry boots. Never 0. Setting 0 gives the
 * default. */
uint32_t settings_timeout(void);
void settings_set_timeout(uint32_t s);

/* Backlight duty in percent, 10 (BACKLIGHT_MIN) to 100. A value outside the
 * range gives the default. */
#define BACKLIGHT_MIN  10u
uint32_t settings_backlight(void);
void settings_set_backlight(uint32_t percent);

/* 1 for the dark theme, 0 for the light theme */
int settings_dark(void);
void settings_set_dark(int on);

/* The saved state of a CFW component: 1 on, 0 off, -1 not in the file. */
int settings_cfw(const char *id);
void settings_set_cfw(const char *id, int on);

/* Writes the file. It keeps the keys that this version does not know, so the
 * settings of a newer version survive. Comments and blank lines are not
 * kept. Returns 0, or a negative code from storage_save(). */
int settings_save(void);
