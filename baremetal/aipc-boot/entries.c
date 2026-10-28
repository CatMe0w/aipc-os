#include <string.h>
#include "entries.h"
#include "ini.h"
#include "lvgl/lvgl.h"
#include "storage.h"

struct entry entries[ENTRIES_MAX];
int entry_count;

static char text[4096];
static char message[64];

struct parse {
    const char *section;  /* of the entry that the next key belongs to */
    int has_type;
};

/* Checks the entry that the parser just finished. */
static void finish(struct parse *p)
{
    struct entry *e;

    if (!p->section)
        return;
    e = &entries[entry_count - 1];
    if (!e->error[0] && !p->has_type)
        lv_strlcpy(e->error, "The entry has no type key.", sizeof(e->error));
    else if (!e->error[0] && !e->kernel[0])
        lv_strlcpy(e->error, "The entry has no kernel key.", sizeof(e->error));
}

static int on_key(void *ctx, const char *section, const char *key,
                  const char *value, int line)
{
    struct parse *p = ctx;
    struct entry *e;

    (void)line;
    if (section != p->section) {
        finish(p);
        if (entry_count == ENTRIES_MAX)
            return -1;
        e = &entries[entry_count++];
        memset(e, 0, sizeof(*e));
        lv_strlcpy(e->title, section, sizeof(e->title));
        p->section = section;
        p->has_type = 0;
    }
    e = &entries[entry_count - 1];

    /* Unknown keys are ignored, so a newer file still loads. */
    if (!strcmp(key, "type")) {
        p->has_type = 1;
        if (strcmp(value, "linux"))
            lv_strlcpy(e->error, "The type is not linux.", sizeof(e->error));
    } else if (!strcmp(key, "kernel")) {
        lv_strlcpy(e->kernel, value, sizeof(e->kernel));
    } else if (!strcmp(key, "cmdline")) {
        lv_strlcpy(e->cmdline, value, sizeof(e->cmdline));
    } else if (!strcmp(key, "description")) {
        lv_strlcpy(e->description, value, sizeof(e->description));
    }
    return 0;
}

const char *entries_load(void)
{
    struct parse p = { 0 };
    uint32_t size;
    int rc;

    entry_count = 0;
    rc = storage_load(ENTRIES_PATH, text, sizeof(text) - 1, &size);
    if (rc == STORAGE_ENOTFOUND)
        return NULL;
    if (rc == STORAGE_ESIZE)
        return "entries.ini is empty or larger than 4 KB.";
    if (rc)
        return "The SD card could not read entries.ini.";
    text[size] = 0;
    rc = ini_parse(text, on_key, &p);
    if (rc == -1) {
        finish(&p);
        lv_snprintf(message, sizeof(message),
                 "entries.ini has more than %d entries.", ENTRIES_MAX);
        return message;
    }
    /* The parse stops at a line that is not valid. That line belongs to the
     * last entry, or comes before the first one. */
    if (rc > 0 && entry_count) {
        struct entry *e = &entries[entry_count - 1];

        lv_snprintf(e->error, sizeof(e->error),
                 "Line %d of entries.ini is not valid.", rc);
        return NULL;
    }
    finish(&p);
    if (rc > 0) {
        lv_snprintf(message, sizeof(message),
                 "Line %d of entries.ini is not valid.", rc);
        return message;
    }
    return NULL;
}
