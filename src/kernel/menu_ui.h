/*
 * BareMetal UI, the graphical home menu in the style of a console home
 * screen: tabs at the top (Games, Dev) and Settings after them, the covers
 * in a grid that scrolls down, moved through with the four directions;
 * the background is the blurred cover of the selected cartridge. Panels
 * (submenus) open over the grid. 640x360 RGB565.
 */
#ifndef MENU_UI_H
#define MENU_UI_H

#include <stdint.h>

#include "drivers/fb.h"
#include "bm/gfx16.h"

typedef struct {
    const char *title;
    const char *author;
    const char *path;           /* file on the SD card, or the built-in name */
    const char *kind;           /* "bm" or "s32" */
    uint32_t size;              /* bytes */
    const g16_sheet_t *cover;   /* 128x80, or NULL for a plain card */
    int running;                /* suspended in memory: a "Playing" badge */
} menu_item_t;

#define MENU_COLS 4             /* covers per row */

/* A row of a panel (a submenu over the grid). */
enum {
    MENU_ROW_ACTION,            /* A does something */
    MENU_ROW_SUB,               /* A opens another panel: "label  value >" */
    MENU_ROW_CHOICE,            /* left/right (or A) change the value: "< value >" */
    MENU_ROW_INFO,              /* only shows a value */
};

typedef struct {
    const char *label;
    const char *value;          /* on the right, or NULL */
    int kind;
} menu_row_t;

#define MENU_PANEL_ROWS 6       /* rows visible at once; the list scrolls */

typedef struct {
    const char *title;          /* "Settings > Controllers" */
    const menu_row_t *rows;
    int n, sel, top;            /* top: first visible row */
    const char *help;           /* under the rows: about the selected one */
} menu_panel_t;

/* What each player plays with, and the network, for the icons of the bar */
enum { MENU_DEV_NONE, MENU_DEV_KEYBOARD, MENU_DEV_PAD };
enum { MENU_NET_NONE, MENU_NET_WIFI, MENU_NET_ETHERNET };

typedef struct {
    const char *const *tabs;    /* tab names */
    int ntabs, tab;             /* current tab */
    int on_gear;                /* Settings is the tab (its panel is open): Games and Dev off */
    const menu_item_t *items;   /* of the current tab */
    int n, sel;
    int dev[4];                 /* players 1-4: MENU_DEV_*, an icon with the number */
    unsigned bt;                /* bit p: player p+1 is on Bluetooth (a blue number) */
    int kbd_bt;                 /* a Bluetooth keyboard types for player kbd_bt (1-4),
                                   0: none; its own icon, a blue number */
    int net;                    /* MENU_NET_*: the link's icon... */
    int net_wait;               /* ...dimmed while there is no address yet */
    const char *details;        /* line under the grid (path, size) */
    const char *note;           /* last game, errors */
    const menu_panel_t *panel;  /* a submenu over the grid, or NULL */
    const char *ask;            /* a question over everything (A yes, B no), or NULL */
    const char *ask_detail;
    const char *ask_yes;        /* the label of A ("Close it", "Delete") */
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
/* A cover from RGBA8888 pixels (the .bm COVER section). */
int  menu_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h);

/* The cover of a development tool: an icon drawn over a colour, the name
 * under it. */
enum {
    MENU_ICON_TERMINAL, MENU_ICON_LUA, MENU_ICON_CHIP, MENU_ICON_LOG, MENU_ICON_PAD,
    MENU_ICON_SOUND, MENU_ICON_GAUGE, MENU_ICON_TRIANGLES, MENU_ICON_FLAME,
    MENU_ICON_ARROWS, MENU_ICON_PLAY, MENU_ICON_BARS, MENU_ICON_CHECK,
};
int  menu_make_tool_cover(g16_sheet_t *s, const char *title, int icon, uint32_t rgb);

#endif
