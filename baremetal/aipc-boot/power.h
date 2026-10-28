#pragma once

/* Turns the device off. A power key that is down holds the device on, so
 * the device turns off when the key comes up. On USB power the device stays
 * on, and the call shows a screen that asks the user to unplug the cable. */
void power_off(void) __attribute__((noreturn));

/* Restarts through the bootrom. See docs/aipc-os-original/warm-restart.md. */
void power_restart(void) __attribute__((noreturn));
