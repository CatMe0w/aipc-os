#pragma once

/* The battery voltage in mV, from the AD5 input of ADC1, or a negative code.
 * Without a battery it reads the charger output. Takes about 20 ms. See
 * docs/nk/battery-driver.md. */
int battery_mv(void);
