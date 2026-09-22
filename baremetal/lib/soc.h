#pragma once
#include <stdint.h>

void uart_init(void);
void uart_putc(char c);

void power_hold(void);
void work_led(uint32_t on);
void speaker_amp(uint32_t on);

void l2_init(void);
void nf_hw_init(void);

void handoff_eboot(void) __attribute__((noreturn));
void handoff_bare(uint32_t addr) __attribute__((noreturn));
