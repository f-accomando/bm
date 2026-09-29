/*
 * The graphical cartridge menu, in the style of a console home screen:
 * tabs at the top (Games, Dev), the covers in a grid that scrolls down,
 * moved through with the four directions; the background is the blurred
 * cover of the selected cartridge. 640x360 RGB565.
 */
#ifndef MENU_UI_H
#define MENU_UI_H

#include <stdint.h>

#include "drivers/fb.h"
#include "b33/gfx16.h"

typedef struct {
    const char *title;
    const char *author;
    const char *path;           /* file on the SD card, or the built-in name */
    const char *kind;           /* "b33" or "s32" */
    uint32_t size;              /* bytes */
    const g16_sheet_t *cover;   /* 128x80, or NULL for a plain card */
    int running;                /* suspended in memory: a "Playing" badge */
} menu_item_t;

#define MENU_COLS 4             /* covers per row */

typedef struct {
    const char *const *tabs;    /* tab names */
    int ntabs, tab;             /* current tab */
    int on_tabs;                /* the focus is on the tab bar */
    const menu_item_t *items;   /* of the current tab */
    int n, sel;
    const char *pads;           /* "pads: 1 - - -" */
    const char *details;        /* line under the grid (path, size) */
    const char *note;           /* last game, errors */
    const char *ask;            /* a question over the menu (A yes, B no), or NULL */
    const char *ask_detail;
} menu_view_t;

/* Switches the screen to the menu mode; -1 if it cannot (the caller keeps
 * the text menu). */
int  menu_ui_open(framebuffer_t *fb);
/* One frame: draws and shows it, waits for the next frame time. */
void menu_ui_frame(framebuffer_t *fb, const menu_view_t *v);
/* Back to the console mode. */
void menu_ui_close(framebuffer_t *fb);

/* A cover for cartridges without one: the title on a coloured label. */
int  menu_make_cover(g16_sheet_t *s, const char *title, const char *kind);
/* A cover from RGBA8888 pixels (the .b33 COVER section). */
int  menu_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h);

#endif
