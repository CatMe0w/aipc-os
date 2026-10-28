#include <string.h>
#include "fade.h"
#include "lcd.h"
#include "log.h"
#include "payload.h"
#include "storage.h"

char payload_names[PAYLOAD_MAX][64];
int payload_count;

#define DDR_LOW         0x30000000u
#define DDR_HIGH        0x34000000u
/* aipc-boot code, heap and stack, which run the load. */
#define SELF_LOW        0x33000000u
#define SELF_HIGH       0x33800000u

#define PT_LOAD         1u
#define EM_ARM          40u
#define ET_EXEC         2u
#define PHNUM_MAX       16u

struct elf32_ehdr {
    uint8_t  ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct elf32_phdr {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};

static int overlaps(uint32_t a, uint32_t b, uint32_t lo, uint32_t hi)
{
    return a < hi && b > lo;
}

/* The segment must stay in DDR and away from what is in use until the
 * jump: aipc-boot itself and the framebuffer that shows the GUI menu. */
static int range_ok(uint32_t addr, uint32_t size)
{
    uint32_t end = addr + size;

    return end >= addr && addr >= DDR_LOW && end <= DDR_HIGH &&
           !overlaps(addr, end, SELF_LOW, SELF_HIGH) &&
           !overlaps(addr, end, FB_ADDR, FB_ADDR + FB_BYTES) &&
           !overlaps(addr, end, FB_BACK_ADDR, FB_BACK_ADDR + FB_BYTES);
}

void payload_list(void)
{
    DIR dir;
    FILINFO fi;

    payload_count = 0;
    if (f_opendir(&dir, PAYLOAD_DIR) != FR_OK)
        return;
    while (payload_count < PAYLOAD_MAX && f_readdir(&dir, &fi) == FR_OK &&
           fi.fname[0]) {
        int i;

        if ((fi.fattrib & (AM_DIR | AM_HID | AM_SYS)) || fi.fname[0] == '.' ||
            strlen(fi.fname) >= sizeof(payload_names[0]))
            continue;
        /* Insertion sort by name. */
        for (i = payload_count; i > 0 &&
             strcmp(payload_names[i - 1], fi.fname) > 0; i--)
            memcpy(payload_names[i], payload_names[i - 1],
                   sizeof(payload_names[0]));
        strcpy(payload_names[i], fi.fname);
        payload_count++;
    }
    f_closedir(&dir);
}

static int read_at(FIL *f, uint32_t off, void *dst, uint32_t len)
{
    if (f_lseek(f, off) != FR_OK)
        return STORAGE_EIO;
    return storage_read(f, dst, len);
}

static int check(FIL *f, struct elf32_ehdr *eh, struct elf32_phdr *ph)
{
    int entry_ok = 0;

    if (read_at(f, 0, eh, sizeof(*eh)))
        return STORAGE_EIO;
    if (memcmp(eh->ident, "\177ELF\1\1", 6) || eh->type != ET_EXEC ||
        eh->machine != EM_ARM || eh->phentsize != sizeof(*ph) ||
        eh->phnum == 0 || eh->phnum > PHNUM_MAX)
        return PAYLOAD_ENOTELF;
    if (read_at(f, eh->phoff, ph, eh->phnum * sizeof(*ph)))
        return STORAGE_EIO;

    for (uint32_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD || ph[i].memsz == 0)
            continue;
        if (ph[i].filesz > ph[i].memsz ||
            !range_ok(ph[i].paddr, ph[i].memsz))
            return PAYLOAD_ERANGE;
        if (eh->entry >= ph[i].paddr &&
            eh->entry < ph[i].paddr + ph[i].memsz)
            entry_ok = 1;
    }
    return entry_ok ? 0 : PAYLOAD_ERANGE;
}

int payload_load(const char *name, uint32_t *entry)
{
    static char path[sizeof(PAYLOAD_DIR) + 1 + sizeof(payload_names[0])];
    static struct elf32_phdr ph[PHNUM_MAX];
    struct elf32_ehdr eh;
    FIL f;
    int rc;

    strcpy(path, PAYLOAD_DIR "/");
    strcat(path, name);
    if (f_open(&f, path, FA_READ) != FR_OK)
        return STORAGE_ENOTFOUND;

    rc = check(&f, &eh, ph);
    if (rc) {
        f_close(&f);
        return rc;
    }

    /* A segment can cover the log window. */
    log_detach();
    for (uint32_t i = 0; i < eh.phnum; i++) {
        uint8_t *dst = (uint8_t *)(uintptr_t)ph[i].paddr;

        if (ph[i].type != PT_LOAD || ph[i].memsz == 0)
            continue;
        if (ph[i].filesz && read_at(&f, ph[i].offset, dst, ph[i].filesz)) {
            f_close(&f);
            return STORAGE_EIO;
        }
        memset(dst + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
    }
    f_close(&f);
    *entry = eh.entry;
    return 0;
}
