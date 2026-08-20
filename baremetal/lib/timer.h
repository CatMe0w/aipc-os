#pragma once
#include <stdint.h>

void timer_init(void);

/* Leaves timer2 disabled for the next payload. */
void timer_stop(void);

/* Milliseconds since timer_init(). Call it more than once every 5.5 s, because
 * the 26-bit hardware counter wraps at that point. */
uint32_t timer_ms(void);

void timer_delay_ms(uint32_t ms);
