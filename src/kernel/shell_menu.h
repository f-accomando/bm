/*
 * The menu of the bm shell: the screen the Monitor shows first. The model (keys, selection) is
 * in monitor_cmd.c (smenu_*); this file has the list of items and draws them on the console.
 */
#ifndef SHELL_MENU_H
#define SHELL_MENU_H

#include "monitor_cmd.h"

/* The items, in the order of the menu. The first one has no line: it opens the bm: prompt. */
const smenu_item_t *shell_menu_items(int *n);

/* Draws the menu on the console (the screen only: the serial line and the network console do not
 * get the box). */
void shell_menu_draw(const smenu_t *m);

#endif
