/*
 * BareMetal UI, the graphical home menu in the style of a console home
 * screen: tabs at the top (Market, Games, Dev) and Settings after them, the covers
 * in a grid that scrolls down, moved through with the four directions;
 * the background is the blurred cover of the selected cartridge. Panels
 * (submenus) open over the grid. RGB565, 640x360 on the Pi (1920x1080 with
 * menu_scale=3: the same layout three times bigger) and 360x360 on the
 * RGB30 (shown 2x): the same drawing, the columns follow the width.
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
    const char *kind;           /* "bm", or "tool" in the Dev tab */
    uint32_t size;              /* bytes */
    const g16_sheet_t *cover;   /* MENU_CARD x MENU_CARD, or NULL for a plain card */
    int running;                /* suspended in memory: a "Playing" badge */
    int loading;                /* no cover yet (the Market): a placeholder with the title */
    const char *badge;          /* a pill on the cover ("Installed"), or NULL */
    int busy, percent;          /* downloading: a bar on the cover */
} menu_item_t;

/* the covers: squares (2026-10-04, the bm Suite's icons; 128x80 before),
 * MENU_COLS a row on the Pi's 640 pixels, 3 on the RGB30's 360 */
#define MENU_CARD 88
#define MENU_COLS 6
/* covers per row of the open menu: 4 on the Pi, 2 on the RGB30 */
int menu_ui_cols(void);

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

/* Settings as a page of its own (2026-10-04), not a panel over the covers:
 * on the Pi the sections on the left and the chosen one's rows on the right
 * (a preview while the sections have the focus); on the RGB30's narrow
 * screen the focused list alone. */
typedef struct {
    const menu_panel_t *sections;   /* the left column (Controllers, ... System) */
    const menu_panel_t *rows;       /* the section's rows, or NULL; sel < 0: none chosen */
    int focus;                      /* 0: the sections, 1: the rows */
} menu_page_t;

/* The Lib tab (docs/RISORSE.md): the groups along the top, the list on
 * the left (a grey row for each file, then its resources), the preview and
 * the details on the right. */
typedef struct {
    const char *label;
    const char *value;          /* right-aligned: a file's count, an item's tag */
    int header;                 /* a file's row (not selectable) */
} menu_lib_row_t;

#define MENU_LIB_ROWS 13        /* rows of the list visible at once */

typedef struct {
    const char *const *groups;
    int ngroups, group;
    const menu_lib_row_t *rows;
    int n, sel, top;            /* sel: the selected row (an item), top: the first one shown */
    const char *empty;          /* why the list is empty */
    const char *lines[5];       /* under the preview: name, numbers, file, author and licence, tags */
    const char *open;           /* A's label ("Open in bm Studio"), or NULL */
    const char *play;           /* Y's label ("Play"), or NULL */
    /* draws the preview in the box (x, y, w, h), or NULL */
    void (*preview)(g16_t *g, int x, int y, int w, int h, void *ctx);
    void *ctx;
} menu_lib_t;

/* What each player plays with, and the network, for the icons of the bar */
enum { MENU_DEV_NONE, MENU_DEV_KEYBOARD, MENU_DEV_PAD };
enum { MENU_NET_NONE, MENU_NET_WIFI, MENU_NET_ETHERNET };
/* Whose buttons the hints at the bottom show (prompts.c); RGB30: the
 * console's own A B X Y, the letters in their colours */
enum { MENU_PROMPTS_DS4, MENU_PROMPTS_KEYBOARD, MENU_PROMPTS_PAD, MENU_PROMPTS_RGB30 };

typedef struct {
    const char *const *tabs;    /* tab names */
    int ntabs, tab;             /* current tab */
    int peek_first;             /* the first tab (the Market) waits off the screen at the left,
                                 * a little of its name showing, until it is the tab */
    int on_gear;                /* Settings is the tab (its panel is open): Games and Dev off */
    const menu_item_t *items;   /* of the current tab */
    int n, sel;
    int dev[4];                 /* players 1-4: MENU_DEV_*, an icon each (no number) */
    unsigned bt;                /* bit p: player p+1 is on Bluetooth (a blue dot) */
    unsigned mice;              /* POINTER_USB / POINTER_BLUETOOTH: a mouse icon each */
    int net;                    /* MENU_NET_*: the link's icon... */
    int net_wait;               /* ...dimmed while there is no address yet */
    int battery;                /* a battery (the RGB30): its icon at the right end... */
    int battery_pct;            /* ...its charge, 0..100 (red when low)... */
    int charging;               /* ...and the bolt on the charger */
    int prompts;                /* MENU_PROMPTS_*: the device pressed last... */
    int prompts_colour;         /* ...the DS4's face buttons in their colours */
    int confirm_b;              /* MENU_PROMPTS_PAD, _RGB30: confirm is B, back A (the RGB30) */
    int no_monitor;             /* no monitor to go to (the RGB30): no hint for it */
    int keys_help;              /* F12 is held: the keys over everything (the system's, the menu's) */
    const char *details;        /* line under the grid (path, size) */
    const char *note;           /* last game, errors */
    const menu_panel_t *panel;  /* a submenu over the grid, or NULL */
    const menu_page_t *page;    /* Settings: a page instead of the grid, or NULL */
    const menu_lib_t *lib;      /* the Lib tab instead of the grid, or NULL */
    const char *ask;            /* a question over everything (A yes, B no), or NULL */
    const char *ask_detail;
    const char *ask_yes;        /* the label of A ("Close it", "Delete") */
    const char *banner;         /* instead of the selected title ("Loading..."), or NULL */
    /* the system's notice over everything (notice.c: a kernel arriving, the
     * restart counted down), or NULL; its line, and a bar (0..1000) or -1 */
    const char *notice;
    const char *notice_detail;
    int notice_progress;
    const char *a_label;        /* A in the footer, instead of Play / Open; "" hides it */
    /* called while the frame waits for its time, until timer_ticks() is
     * `until` (the Market's downloads), or NULL */
    void (*idle)(uint32_t until);
} menu_view_t;

/* What is under a point of the last frame (M32: the pointer), the
 * topmost thing: a cover (index; full = not cut by the edges of the grid),
 * a tab (index), Settings, a row of the panel (index in its rows), a
 * button of the hints (index 'A', 'B', 'X' or 'Y'), the panel or the
 * question elsewhere, a group or a row of the Lib tab (index), a section of
 * the Settings page (index), or nothing. */
enum { MENU_HIT_NONE, MENU_HIT_COVER, MENU_HIT_TAB, MENU_HIT_SETTINGS, MENU_HIT_ROW,
       MENU_HIT_BUTTON, MENU_HIT_PANEL, MENU_HIT_ASK, MENU_HIT_GROUP, MENU_HIT_LIB,
       MENU_HIT_SECTION };         /* a section of the Settings page while its rows have the focus */
typedef struct { int kind, index, full; } menu_hit_t;
menu_hit_t menu_ui_hit(int x, int y);

/* Switches the screen to the menu mode (it has the pointer); -1 if it
 * cannot (the caller keeps the text menu). */
int  menu_ui_open(framebuffer_t *fb);
/* One frame: draws and shows it, waits for the next frame time. */
void menu_ui_frame(framebuffer_t *fb, const menu_view_t *v);
/* Back to the console mode. */
void menu_ui_close(framebuffer_t *fb);
/* The same, the console left suspended: a game's loading screen takes the
 * screen at once, without the log in between */
void menu_ui_close_quiet(framebuffer_t *fb);

/* A cover for cartridges without one: the title on a coloured label. */
int  menu_make_cover(g16_sheet_t *s, const char *title, const char *kind);
/* A cover from RGBA8888 pixels (the .bm COVER section, a Market PNG), any
 * size: a square scaled to the card; another shape (the 128x80 covers of
 * before) whole, as wide as the card, over a blurred darker copy of it. */
int  menu_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h);

/* The cover of a development tool: an icon drawn over a colour, the name
 * under it. */
enum {
    MENU_ICON_TERMINAL, MENU_ICON_LUA, MENU_ICON_CHIP, MENU_ICON_LOG, MENU_ICON_PAD,
    MENU_ICON_SOUND, MENU_ICON_GAUGE, MENU_ICON_TRIANGLES, MENU_ICON_FLAME,
    MENU_ICON_ARROWS, MENU_ICON_PLAY, MENU_ICON_BARS, MENU_ICON_CHECK, MENU_ICON_ASSIST,
    MENU_ICON_CODE,
};
int  menu_make_tool_cover(g16_sheet_t *s, const char *title, int icon, uint32_t rgb);

#endif
