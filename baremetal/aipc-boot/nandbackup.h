#pragma once
#include <stdint.h>

enum {
    NANDBACKUP_OK = 0,
    NANDBACKUP_CANCELLED,
    NANDBACKUP_ENAND,       /* a NAND read failed, or the part is not known */
    NANDBACKUP_ECARD,       /* a write to the card failed. A full card is one cause */
    NANDBACKUP_EEXISTS,     /* the same backup is there already. name is set */
    NANDBACKUP_EFORMAT,     /* the file is not an LZ4 frame of a whole NAND image */
};

/* The card and NAND share pads. These give the pads to one side. */
int nandtool_card_select(void);
void nandtool_nand_select(void);

/* Copies all of NAND to /backup on the card, in the format of
 * `opennboot backup-nand`. The card must be mounted. progress() gets the part
 * done in permille and returns nonzero to cancel. name gets the file name of
 * the result, without the directory. */
int nand_backup(int (*progress)(uint32_t permille), char *name, uint32_t size);

struct nand_restore_check {
    int checksum_ok;    /* the contents match the checksum in the file */
    int name_ok;        /* the file name shows that checksum */
    uint32_t lost;      /* blocks with data that are bad on this device */
};

/* Reads the whole image at path and compares it with this NAND. It writes
 * nothing. progress() is that of nand_backup(). */
int nand_restore_check(const char *path, int (*progress)(uint32_t permille),
                       struct nand_restore_check *out);

/* Erases NAND and writes the image at path. It does no check of its own
 * except the size, and it cannot be cancelled. */
int nand_restore_write(const char *path, int (*progress)(uint32_t permille));
