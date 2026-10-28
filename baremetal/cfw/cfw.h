#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cfw_abi.h"
#include "pf.h"

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

/* The two times when CFW has control. */
enum cfw_stage {
    CFW_EBOOT = 1,  /* after aipc-boot loads EBOOT, before EBOOT runs */
    CFW_NK,         /* after EBOOT loads NK, before NK runs */
};

typedef const struct {
    uint8_t stage;
    const char *module;                     /* ROM module for CFW_NK */
    void (*patch)(pf_patchset_t *patchset); /* NULL ends the list */
} cfw_patch_t;

/* info must stay the first member. The header lists the components through
 * pointers to info. */
typedef const struct {
    struct cfw_component_info info;
    void (*init)(void);    /* before the first patch in a stage */
    void (*finish)(void);  /* after the last patch in a stage */
    cfw_patch_t patches[];
} cfw_component_t;

extern cfw_component_t *const cfw_components[];
extern const uint32_t cfw_component_count;

/* Set by the eboot stage, read by the nk stage. It stays in .data, see
 * cfw.ld. */
extern uint32_t cfw_enable;
extern enum cfw_stage cfw_stage;

/* The range that the current patchset scans. */
extern pf_range_t *cfw_range;

/* Runs the patches of each enabled component that belong to stage.
 * range_of() gives the range for the module of a patch, or panics. */
void cfw_run(enum cfw_stage stage,
             void (*range_of)(const char *module, pf_range_t *range));

void printf(const char *fmt, ...);

/* In the eboot stage, panic() returns from eboot_stage() to aipc-boot with
 * the message. eboot_stage() sets the return point. */
extern struct cfw_result *cfw_eboot_result;
void cfw_eboot_return(void) __attribute__((noreturn));

/* Copies the nk resident to NK_RESIDENT_PHYS. */
void cfw_nk_resident_install(void);

/* Logs the patch name, and panics if the pattern matched before. */
void cfw_patch_once(struct pf_patch *patch);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* The pf_maskmatch() mask word that compares all bits */
#define EXACT 0xFFFFFFFFu

/* The literal word that `ldr rX, [pc, #imm]` at ldr loads */
static inline uint32_t *ldr_literal(uint32_t *ldr)
{
    return (uint32_t *)((uintptr_t)ldr + 8u + (*ldr & 0xFFFu));
}

/* The entry of the CFW image, in eboot/entry.S. aipc-boot calls it as a C
 * function, with the MMU and the caches on, on its own stack. */
int32_t eboot_stage(const struct cfw_eboot_args *args,
                    struct cfw_result *result);

extern cfw_component_t cfw_rtc_stall_fix;
extern cfw_component_t cfw_power_hold;
extern cfw_component_t cfw_battery_check_skip;
extern cfw_component_t cfw_diag;
