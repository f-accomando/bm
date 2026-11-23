/*
 * The screen of the bm shell: the screen the Monitor shows first. The model (keys, selection) is in
 * monitor_cmd.c (smenu_*); this file has the list of items and draws the frame, the files, the icon
 * bar and the help line on the console.
 */
#ifndef SHELL_MENU_H
#define SHELL_MENU_H

#include <stdint.h>

#include "monitor_cmd.h"

/* The items, in the order of the bar. The item with no line (Command line) opens the bm: prompt. */
const smenu_item_t *shell_menu_items(int *n);

/* A row of the file list: a folder ("..": the folder above) or a file of the SD card. */
#define SHELL_FILE_NAME 128
typedef struct {
    char name[SHELL_FILE_NAME];
    int is_dir;
    uint32_t size;
} shell_file_t;

/* What Enter does on a row: by the name and is_dir (an extension, case ignored). */
enum { SHELL_KIND_UP, SHELL_KIND_DIR, SHELL_KIND_CART, SHELL_KIND_NES, SHELL_KIND_SNES, SHELL_KIND_FILE };
int shell_file_kind(const shell_file_t *f);

/* Draws the screen of the shell (the screen only: the serial line and the network console do not get
 * it): a frame with the folder `dir` and its m->nfiles rows `files` as a tree (the rows that do not fit
 * scroll with m->filesel); the items as icons in the middle of its bottom border (an open group: its
 * box above its icon); under the border, one line that tells what the selection does. */
void shell_menu_draw(const smenu_t *m, const char *dir, const shell_file_t *files);

/* A frame with rows "label  value" (the system info). */
void shell_info_draw(const char *title, const char (*label)[14], const char (*value)[40], int n);

#endif
