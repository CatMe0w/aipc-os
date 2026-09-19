#pragma once
#include <stdint.h>

/* Every probe core exports these three. The Makefile PROBE variable picks one. */
uint32_t probe_init(void);
void probe_stage(uint32_t stage);
void probe_trigger(void);

#define PROBE_STAGE_ADDR   0x32007F00u
#define PROBE_RESULT_BASE  0x32008000u
