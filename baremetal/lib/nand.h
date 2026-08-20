#pragma once
#include <stdint.h>

enum {
    NAND_OK             =  0,
    NAND_ESEQ_CMD       = -1,
    NAND_ESEQ_DATA      = -2,
    NAND_EL2_FILL       = -3,
    NAND_EDMA_DONE      = -4,
    NAND_EUNCORRECTABLE = -5,
    NAND_EBADPOS        = -6,
    NAND_ENOGEOM        = -7,
    NAND_EPROBE         = -8,
    NAND_ELEN           = -9,
    NAND_EBADRUN        = -10,
};

void nand_init(void);
int nand_probe_geometry(void);
int nand_load_image(void *dst, uint32_t start_block, uint32_t bytes);

#define IPL_START_BLOCK 2u

uint32_t nand_corrections(void);
uint32_t nand_retries(void);
