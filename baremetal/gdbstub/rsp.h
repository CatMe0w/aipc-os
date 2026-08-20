#pragma once
#include <stdint.h>

/* GDB arm.core order. cpsr is regnum 25, in the gap that FPA once had. */
struct arm_regs {
    uint32_t r[13];
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
    uint32_t cpsr;
};

extern struct arm_regs g_regs;

/* Feed bytes from bulk OUT into the packet parser. */
void rsp_feed(const uint8_t *data, uint32_t len);

/* Restore g_regs and branch to its pc. Does not return. */
void rsp_resume(struct arm_regs *regs) __attribute__((noreturn));

/* Exception entry points (bp.S). */
void bp_undef(void);
void bp_pabort(void);
void bp_dabort(void);

/* Point the forwarded ROM vectors at the handlers above. */
void bp_install(void);

/* Next pc after the instruction at g_regs.pc. Returns 0 if the target is
 * undecidable, which is what a Thumb pc gives. */
uint32_t arm_next_pc(uint32_t *next);

/* Report the stop to GDB and serve it from there. Does not return. */
void rsp_trapped(uint32_t sig) __attribute__((noreturn));
