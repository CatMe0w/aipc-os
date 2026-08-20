#pragma once

/* Builds the menu and runs it. Never returns: a failed boot attempt reports
 * its return code and goes back to the menu. */
void ui_run(int sd_rc);

/* Draws the menu onto the active LVGL screen. */
void ui_build(void);
