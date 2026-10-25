/*
 * The game library: the cartridges of the SD card, as the menu of the Pi
 * (carts.c) and the menu of the RGB30 (src/rgb30/ui.c) list them. One model
 * and one set of functions for both. What differs between the two consoles
 * stays behind the hooks of lib_backend_t.
 */
#ifndef LIBRARY_H
#define LIBRARY_H

#include <stddef.h>
#include <stdint.h>
#include "bm/gfx16.h"
#include "fs/fat.h"
#include "fiber.h"

#define LIB_MAX 64              /* cartridges in the library (the SD card and the built-in ones) */

/* One cartridge of the library. */
typedef struct {
    char title[49];             /* from the header; the file name if none */
    char author[33];
    char name[FAT_NAME_MAX];
    char dir[8];                /* "" for built-in, "/", "/carts", "/bm" or the projects folder */
    const uint8_t *builtin;     /* NULL: file on SD */
    uint32_t size;
    fat_entry_t fe;
    g16_sheet_t cover;          /* printed on the card in the menu (px NULL: none) */
    uint8_t cover_read;         /* the covers' job has been at it (else a placeholder) */
    char path[FAT_NAME_MAX + 10];
    char save[32];              /* its save file (.bm), "" if none */
    const char *tool;           /* the tool id ("sdk", "sound"...) of a tool cartridge (toolnames.h), or NULL */
    uint8_t type;               /* BM_TYPE_* from the header flags (bm.info "type:"): an app or a utility is listed in Dev */
    uint8_t grey;               /* not made for this kernel (bm_compat_classify): faded, starts after a question */
    const char *why;            /* the reason of the grey, a short static text */
} lib_game_t;

/* What each console decides. */
typedef struct {
    const char *const *dirs;            /* the folders to read, in this order; NULL at the end */
    int target;                         /* BM_TARGET_PI or BM_TARGET_RGB30: the compatibility policy */
    /* Is the file listed? 1 yes, 0 no, -1 left out on purpose (counted in lib_t.hidden). NULL: lib_ext_ok. */
    int (*want)(const char *name);
    /* The built-in cartridges, added after the files (sorted by title before them). NULL: none. */
    void (*add_builtins)(void);
    /* Before the list changes: the jobs that hold a cartridge of it stop. */
    void (*stop_background)(void);
} lib_backend_t;

typedef struct {
    lib_game_t g[LIB_MAX];
    int n;                      /* cartridges in g[] */
    int nsd;                    /* the ones from the SD card (the built-in ones follow) */
    int hidden;                 /* files that want() left out on purpose */
    int sd_ok;                  /* the card is mounted */
    const lib_backend_t *be;
    fiber_job_t cover_job;      /* the covers, read in the menu's free time */
    int cover_dev;              /* the list the menu shows: 1 Dev, 0 Games, -1 another (its covers first) */
    int cover_sel;              /* the cartridge under the cursor, or -1 */
} lib_t;

/* The one library of the console. */
extern lib_t library;

/* Compare two titles, ignoring the case of ASCII letters (sort order of the menus). */
int lib_text_cmp(const char *a, const char *b);

/* 1 if the two texts are the same, ignoring the case of ASCII letters. */
int lib_same_text(const char *a, const char *b);

/* qsort comparator of lib_game_t by title. */
int lib_title_cmp(const void *a, const void *b);

/* The file name ends with .bm, .b16 or .bme (any case). */
int lib_ext_ok(const char *name);

/* "bm", "b16" or "bme" (a project): the menu's label of a file. */
const char *lib_kind(const lib_game_t *g);

/* Reads the folders again: the files with their headers (the first 512 bytes: title, author, type,
 * compatibility), sorted by title, then the built-in ones. The covers already read stay. */
void lib_scan(lib_t *l);

/* Adds a built-in cartridge (from add_builtins); `tool`: its tool id, or NULL. */
void lib_add_builtin(lib_t *l, const char *name, const char *tool, const uint8_t *start, const uint8_t *end);

/* The type rule: a development tool (built into the kernel or installed from Market > Apps) and any file
 * cartridge that declares type app or utility in its bm.info are listed in Dev; the rest are games. */
int lib_is_dev(const lib_game_t *g);

/* The SD card cartridge with this path (case ignored), or -1. */
int lib_find_path(const lib_t *l, const char *path);

/* ---- the covers
 *
 * The menu opens at once with the titles (a placeholder with the title); the covers come in a fiber in the
 * menu's free time, the ones on screen first (2026-10-04, the user's request: smooth with any number of
 * games). Of a file only its first bytes are read: COVER is the first section. */

/* What the menu shows, for the order of the covers: dev 1 (Dev list), 0 (Games list) or -1 (Market, Settings);
 * sel: the library index under the cursor, or -1. */
void lib_cover_focus(int dev, int sel);

/* A share of the menu's free time for the covers (fiber_share_t function); starts the job when covers are left. */
void lib_covers_tick(uint32_t until);

/* Stops the job (a game, a page or a change of the list follows); it goes on later where it was. */
void lib_covers_stop(void);

/* How many covers are read. */
int lib_covers_read(const lib_t *l);

#endif
