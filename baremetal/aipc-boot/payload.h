#pragma once

#include <stdint.h>

#define PAYLOAD_DIR  "/aipc-boot/payloads"
#define PAYLOAD_MAX  16

extern char payload_names[PAYLOAD_MAX][64];
extern int payload_count;

/* Lists the directory, sorted by name. A missing directory gives none. */
void payload_list(void);

/* Loads the payload with the given name. Returns 0 and sets *entry, or
 * returns a negative code. payload_call() starts it. */
int payload_load(const char *name, uint32_t *entry);

#define PAYLOAD_ENOTELF   (-40)  /* not a 32-bit little-endian ARM executable */
/* A segment is outside DDR, or covers aipc-boot or a framebuffer. */
#define PAYLOAD_ERANGE    (-41)
