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
#include "drivers/fb.h"
#include "bm/runtime.h"
#include "home.h"

#define LIB_PLAY_SECS   (24u * 3600u)   /* a game runs this long at most (bm_run) */

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
    unsigned panel;                     /* LIB_CAP_*: the rows of a cartridge's panel this console has */
    /* The menu's jobs and their shares of its free time (fiber.h, lib_menu_idle). */
    const fiber_share_t *jobs;
    int njobs;
    void (*write_waits)(void);          /* a file from the PC waits to be written: the jobs it blocks stop. NULL: none. */
    void (*before_jobs)(void);          /* before the jobs run in a frame. NULL: none. */
    /* Just before a cartridge starts or resumes (a WiFi try of the menu's stops, the loading screen shows). NULL: none. */
    void (*play_begin)(const lib_game_t *g, int resume);
} lib_backend_t;

/* The rows a console may have in a cartridge's panel (lib_backend_t.panel). Play, Close, Export, the info rows
 * and the deletes are on every console. */
#define LIB_CAP_SDK     1u      /* Open in the SDK */
#define LIB_CAP_TOOLS   2u      /* Open in bm Code, the Sound editor, bm Studio, bm Animator, bm Mesh, bm Pixel */
#define LIB_CAP_COPY    4u      /* Make an editable copy */
#define LIB_CAP_PUBLISH 8u      /* Publish to the Market */
#define LIB_CAP_SEND    16u     /* Send to a nearby console */

/* The row ids of a cartridge's panel (home_row id; lib_panel and lib_act). */
enum { LIB_ROW_PLAY = 100, LIB_ROW_CLOSE, LIB_ROW_SDK, LIB_ROW_SOUND, LIB_ROW_STUDIO, LIB_ROW_AUTHOR, LIB_ROW_FILE,
       LIB_ROW_SIZE, LIB_ROW_TYPE, LIB_ROW_SAVE, LIB_ROW_DEL_SAVE, LIB_ROW_DELETE, LIB_ROW_CODE, LIB_ROW_MESH,
       LIB_ROW_PIXEL, LIB_ROW_ANIMATOR, LIB_ROW_PUBLISH, LIB_ROW_SEND, LIB_ROW_COPY, LIB_ROW_BUILD_B16 };

typedef struct {
    lib_game_t g[LIB_MAX];
    int n;                      /* cartridges in g[] */
    int nsd;                    /* the ones from the SD card (the built-in ones follow) */
    int hidden;                 /* files that want() left out on purpose */
    int sd_ok;                  /* the card is mounted */
    char susp_path[FAT_NAME_MAX + 10];  /* the cartridge frozen in memory (Start + Select), "" if none */
    const lib_backend_t *be;
    long panel_save;            /* the save data of the cartridge whose panel is open: bytes, -1 if none */
    int panel_slots;            /* ... in how many slots */
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

/* A project (.bme): the editors open it; the tabs do not list it. */
int lib_is_project(const lib_game_t *g);

/* The cartridges a tab lists: the library indexes of the games (dev 0) or of the development tools and apps
 * (dev 1), in the order of the library (by title), no project. Returns how many (idx holds LIB_MAX at most). */
int lib_tab_items(int dev, int *idx);

/* The SD card cartridge with this path (case ignored), or -1. */
int lib_find_path(const lib_t *l, const char *path);

/* ---- the game frozen in memory (a .bm left with Start + Select stays suspended and the menu comes back) */

/* This cartridge is the frozen one. */
int lib_is_suspended(const lib_game_t *g);
/* Another game is frozen: starting this one would lose it, so the menu asks first. */
int lib_need_switch(const lib_game_t *g);
/* The game that was started returned suspended: remember which one. */
void lib_suspend_set(const lib_game_t *g);
/* The frozen game is gone (it ended): forget it. */
void lib_suspend_clear(void);
/* Close the frozen game (its memory is freed) and forget it. */
void lib_suspend_close(void);
/* The words of the question "close the frozen game?" (q: question, d: detail, y: the yes button). */
void lib_switch_question(char *q, size_t qn, char *d, size_t dn, char *y, size_t yn);
/* A game that is not made for this console (docs/spec/compat-policy.toml) asks first: 1 and the words of the
 * question, or 0 (nothing to ask). Grey is not a lock: the confirm button starts the game. */
int lib_compat_question(const lib_game_t *g, char *q, size_t qn, char *d, size_t dn, char *y, size_t yn);

/* ---- the save data of a cartridge */

/* Its save slots: the bytes in all and (slots) how many are used; -1 if there is no save data. */
long lib_save_size(const lib_game_t *g, int *slots);
/* Deletes every slot: 0, or -1 and the first error in note. */
int lib_save_delete(const lib_game_t *g, char *note, size_t n);

/* ---- starting a cartridge */

enum { LIB_PLAY_OK, LIB_PLAY_UNREADABLE = -1, LIB_PLAY_BADFORMAT = -2 };

/* Plays the cartridge, or resumes it if it is the frozen one (st gets the statistics). A game that returns
 * suspended (Start + Select) is remembered. LIB_PLAY_UNREADABLE: the file cannot be read (the reason is
 * fat_error(), the loading screen is stopped); LIB_PLAY_BADFORMAT: it is not a cartridge. The caller shows
 * what comes after: the statistics, the last-played line, an error page. */
int lib_play(framebuffer_t *fb, lib_game_t *g, bm_stats_t *st);

/* ---- the panel of a cartridge (X on its cover): the options as rows, like the Settings */

/* The panel of this cartridge opens: its save data is read (not on every frame). */
void lib_panel_open(const lib_game_t *g);
/* Builds the rows for this cartridge. */
void lib_panel(const lib_game_t *g, home_panel_t *p);

/* A row of the cartridge's panel that stays in the menu (Close, the editable copy, Export .b16, Send, the
 * deletes). `how`: 0 (confirm) or HOME_YES (the question was answered yes). Fills `d` (it is cleared first);
 * HOME_BACK means the list changed: the menu reads the card again. Returns 0 for a row it does not own
 * (Play and the Open in rows: the menu starts those; Publish: the Pi's). */
int lib_act(lib_game_t *g, int row, int how, home_do_t *d);

/* The menu's free time in a frame (menu_view_t.idle): a file from the PC (bm_net.py --send) is written first,
 * alone on the card (2026-10-10); then the jobs of the console, each with its share. */
void lib_menu_idle(uint32_t until);

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
