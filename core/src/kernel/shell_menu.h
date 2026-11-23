/*
 * The menu of the bm shell: the screen the Monitor shows first. The model (keys, selection) is
 * in monitor_cmd.c (smenu_*); this file has the list of items and draws them on the console.
 */
#ifndef SHELL_MENU_H
#define SHELL_MENU_H

#include "monitor_cmd.h"

/* The items, in the order of the menu. The item with no line (Command line) opens the bm: prompt. */
const smenu_item_t *shell_menu_items(int *n);

/* A row of the file list under the menu: a folder ("..": the folder above) or a file of the SD card. */
#define SHELL_FILE_NAME 128
typedef struct {
    char name[SHELL_FILE_NAME];
    int is_dir;
} shell_file_t;

/* Draws the menu on the console (the screen only: the serial line and the network console do not
 * get the box). Under it: the folder `dir` and its m->nfiles rows `files` (a folder icon or a floppy
 * icon, then the name); the rows that do not fit scroll with m->filesel. */
void shell_menu_draw(const smenu_t *m, const char *dir, const shell_file_t *files);

/* A frame with rows "label  value" (the system info). */
void shell_info_draw(const char *title, const char (*label)[14], const char (*value)[40], int n);

#endif
