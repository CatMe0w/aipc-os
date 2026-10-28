#include <string.h>
#include "ini.h"
#include "lvgl/lvgl.h"
#include "settings.h"
#include "storage.h"

#define TEXT_MAX           4096u
#define KEYS_MAX           32
#define TIMEOUT_DEFAULT_S  15u
/* Full duty keeps the PWM still, so the backlight makes no audible
 * whine. */
#define BACKLIGHT_DEFAULT  100u

/* Every key of the file, including the keys that this version does not
 * know, in the order that they first appeared. */
static struct key {
    char section[24];
    char name[32];
    char value[96];
} keys[KEYS_MAX];
static int key_count;

static char text[TEXT_MAX];

static struct key *find(const char *section, const char *name)
{
    for (int i = 0; i < key_count; i++)
        if (!strcmp(keys[i].section, section) && !strcmp(keys[i].name, name))
            return &keys[i];
    return NULL;
}

static const char *get(const char *section, const char *name)
{
    struct key *k = find(section, name);

    return k ? k->value : NULL;
}

/* A NULL value removes the key, so that it takes its default. */
static void set(const char *section, const char *name, const char *value)
{
    struct key *k = find(section, name);

    if (!value) {
        if (k) {
            key_count--;
            memmove(k, k + 1, (size_t)(keys + key_count - k) * sizeof(*k));
        }
        return;
    }
    if (!k) {
        if (key_count == KEYS_MAX)
            return;
        k = &keys[key_count++];
        lv_strlcpy(k->section, section, sizeof(k->section));
        lv_strlcpy(k->name, name, sizeof(k->name));
    }
    lv_strlcpy(k->value, value, sizeof(k->value));
}

static void set_uint(const char *section, const char *name, uint32_t v)
{
    char t[12];

    lv_snprintf(t, sizeof(t), "%u", (unsigned)v);
    set(section, name, t);
}

/* Returns def when the value is missing or is not a number in [min, max]. */
static uint32_t get_uint(const char *section, const char *name, uint32_t min,
                         uint32_t max, uint32_t def)
{
    const char *s = get(section, name);
    uint32_t n = 0;

    if (!s || !*s)
        return def;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > 100000u)
            return def;
        n = n * 10u + (uint32_t)(*s - '0');
    }
    return n >= min && n <= max ? n : def;
}

const char *settings_default(void)
{
    const char *s = get("boot", "default");

    return s ? s : "";
}

void settings_set_default(const char *title)
{
    set("boot", "default", title[0] ? title : NULL);
}

uint32_t settings_timeout(void)
{
    return get_uint("boot", "timeout", 1u, 100000u, TIMEOUT_DEFAULT_S);
}

void settings_set_timeout(uint32_t s)
{
    if (s)
        set_uint("boot", "timeout", s);
    else
        set("boot", "timeout", NULL);
}

uint32_t settings_backlight(void)
{
    return get_uint("display", "backlight", BACKLIGHT_MIN, 100u,
                    BACKLIGHT_DEFAULT);
}

void settings_set_backlight(uint32_t percent)
{
    if (percent >= BACKLIGHT_MIN && percent <= 100u)
        set_uint("display", "backlight", percent);
    else
        set("display", "backlight", NULL);
}

int settings_dark(void)
{
    const char *s = get("display", "theme");

    return s && !strcmp(s, "dark");
}

void settings_set_dark(int on)
{
    set("display", "theme", on ? "dark" : "light");
}

int settings_cfw(const char *id)
{
    const char *s = get("cfw", id);

    if (s && !strcmp(s, "on"))
        return 1;
    if (s && !strcmp(s, "off"))
        return 0;
    return -1;
}

void settings_set_cfw(const char *id, int on)
{
    set("cfw", id, on ? "on" : "off");
}

void settings_reset(void)
{
    key_count = 0;
}

static int on_key(void *ctx, const char *section, const char *name,
                  const char *value, int line)
{
    (void)ctx;
    (void)line;
    set(section, name, value);
    return 0;
}

void settings_load(void)
{
    uint32_t size;

    settings_reset();
    if (storage_load(SETTINGS_PATH, text, TEXT_MAX - 1, &size))
        return;
    text[size] = 0;
    /* A line that is not valid stops the parse. The keys before it count. */
    (void)ini_parse(text, on_key, NULL);
}

int settings_save(void)
{
    uint32_t len = 0;

    for (int i = 0; i < key_count; i++) {
        int first = 1;

        for (int j = 0; j < i; j++)
            if (!strcmp(keys[j].section, keys[i].section))
                first = 0;
        if (!first)
            continue;
        if (keys[i].section[0])
            len += lv_snprintf(text + len, TEXT_MAX - len, "[%s]\n",
                               keys[i].section);
        for (int j = i; j < key_count && len < TEXT_MAX; j++)
            if (!strcmp(keys[j].section, keys[i].section))
                len += lv_snprintf(text + len, TEXT_MAX - len, "%s=%s\n",
                                   keys[j].name, keys[j].value);
        if (len >= TEXT_MAX)
            return STORAGE_ESIZE;
    }
    return storage_save(SETTINGS_PATH, text, len);
}
