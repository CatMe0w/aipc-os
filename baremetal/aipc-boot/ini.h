#pragma once

/* Called once for each key. Returns 0 to continue. */
typedef int (*ini_handler)(void *ctx, const char *section, const char *key,
                           const char *value, int line);

/* Parses lines of `[section]`, `key=value`, and comments that start with ';'
 * or '#'. Spaces around names and values are ignored. Parses text in place:
 * the call writes NUL bytes into it. Returns 0, the
 * first nonzero handler result, or the number of a line that is not valid. */
int ini_parse(char *text, ini_handler handler, void *ctx);
