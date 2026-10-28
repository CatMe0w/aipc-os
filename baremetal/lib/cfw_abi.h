#pragma once

/*
 * The binary interface between aipc-boot and CFW. aipc-boot carries the CFW
 * image, copies it to CFW_PHYS, and calls eboot_stage. The image starts with
 * struct cfw_header.
 */

/* Linker scripts take the constants without the C suffix. */
#ifdef __ASSEMBLER__
#define CFW_U(x)  x
#else
#define CFW_U(x)  x##u
#endif

/* The image, including .bss, must end at or below CFW_PHYS + CFW_MAX. */
#define CFW_PHYS         CFW_U(0x30190000)
#define CFW_MAX          CFW_U(0x0000A000)

/* cfw_component_info.flags */
#define CFW_FIX          (1u << 0)
#define CFW_EXTRA        (1u << 1)
#define CFW_DEFAULT_ON   (1u << 2)

#ifndef __ASSEMBLER__
#include <stdint.h>

struct cfw_component_info {
    char id[32];   /* also the key in settings.ini */
    char name[32];
    char description[160];  /* one or two lines in the CFW page */
    uint32_t flags;
};

struct cfw_eboot_args {
    uint32_t code;    /* EBOOT code in DDR, which executes at 0x80038000 */
    uint32_t bytes;
    uint32_t enable;  /* bit n enables components[n] */
};

struct cfw_result {
    int32_t status;   /* 0, or negative when the stage failed */
    char message[96];
};

struct cfw_header {
    /* Patches EBOOT in place and returns result->status. The caller hands
     * off to EBOOT only when the status is 0. */
    int32_t (*eboot_stage)(const struct cfw_eboot_args *args,
                           struct cfw_result *result);
    uint32_t component_count;
    const struct cfw_component_info *const *components;
};
#endif
