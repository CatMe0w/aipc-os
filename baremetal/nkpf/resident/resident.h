#pragma once

#define RESIDENT_MAGIC 0x49534552u  /* "RESI" */

#ifndef __ASSEMBLER__
#include <stdint.h>

/* All fields are WinCE VAs. nkpf writes the original addresses into the words
 * that irq_orig and ioctl_resume point to. */
struct resident_hdr {
    uint32_t magic;
    uint32_t irq_hook;
    uint32_t irq_orig;
    uint32_t ioctl_hook;
    uint32_t ioctl_resume;  /* OEMIoControl + 4 */
};
#endif
