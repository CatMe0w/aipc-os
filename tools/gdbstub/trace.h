#pragma once
#include <stdint.h>

/* The live log moves to TRACE_PREV at every startup. */
#define TRACE_BASE 0x301C0000u
#define TRACE_PREV 0x301D0000u
#define TRACE_SIZE 0x00010000u

void trace_init(void);
void trace_puts(const char *s);
void trace_hex(uint32_t value, uint32_t digits);
void trace_dec(uint32_t value);

/* "name=0xVALUE\n" */
void trace_reg(const char *name, uint32_t value);

/* Bytes written so far. The buffer is linear and does not wrap. */
uint32_t trace_used(void);

/* Length of the preserved log, found by looking for its terminator. */
uint32_t trace_prev_used(void);
