#pragma once

#define ENTRIES_PATH   "/aipc-boot/entries.ini"
#define ENTRIES_MAX    8

struct entry {
    char title[48];
    char kernel[96];        /* path from the root of the card */
    char cmdline[256];      /* empty = no command line */
    char description[96];
    char error[48];         /* not empty = the entry cannot boot */
};

extern struct entry entries[ENTRIES_MAX];
extern int entry_count;

/* Reads entries.ini, see README.md for the format. Without the file, there
 * are no entries. Returns NULL, or a message about the whole file. A problem
 * in one entry goes into its error field. */
const char *entries_load(void);
