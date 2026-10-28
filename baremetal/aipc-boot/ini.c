#include <stddef.h>
#include <string.h>
#include "ini.h"

static char *trim(char *s, char *end)
{
    while (s < end && (*s == ' ' || *s == '\t'))
        s++;
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    *end = 0;
    return s;
}

int ini_parse(char *text, ini_handler handler, void *ctx)
{
    const char *section = "";
    int line = 0;

    while (*text) {
        char *end = text, *next;
        char *s, *eq;

        while (*end && *end != '\n')
            end++;
        next = *end ? end + 1 : end;
        line++;
        s = trim(text, end);

        if (*s == 0 || *s == ';' || *s == '#') {
            /* blank or comment */
        } else if (*s == '[') {
            char *close = s;

            while (*close && *close != ']')
                close++;
            if (*close != ']')
                return line;
            section = trim(s + 1, close);
        } else {
            char *value;
            int rc;

            for (eq = s; *eq && *eq != '='; eq++)
                ;
            if (*eq != '=')
                return line;
            *eq = 0;
            value = eq + 1;
            rc = handler(ctx, section, trim(s, eq),
                         trim(value, value + strlen(value)), line);
            if (rc)
                return rc;
        }
        text = next;
    }
    return 0;
}
