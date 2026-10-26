/*
 * BareMetal UI, the graphical home menu in the style of a console home
 * screen: tabs at the top (Market, Games, Dev) and Settings after them, the covers
 * in a grid that scrolls down, moved through with the four directions;
 * the background is the blurred cover of the selected cartridge. Panels
 * (submenus) open over the grid. RGB565, 640x360 on the Pi, shown 2x at 1280x720 when the GPU is there
 * (menu_scale=3: 1920x1080) and 360x360 on the
 * RGB30 (shown 2x): the same drawing, the columns follow the width.
 */
#ifndef MENU_UI_H
#define MENU_UI_H

#include <stdint.h>

#include "drivers/fb.h"
#include "bm/gfx16.h"

typedef struct {
    const char *title;
    const char *path;           /* file on the SD card, or the built-in name */
    const char *kind;           /* "bm", or "tool" in the Dev tab */
    const g16_sheet_t *cover;   /* MENU_CARD x MENU_CARD, or NULL for a plain card */
    int running;                /* suspended in memory: a star at the top right */
    int update;                 /* a newer version is in the Market: a triangle at the bottom right */
    int loading;                /* no cover yet (the Market): a placeholder with the title */
    const char *badge;          /* a pill on the cover ("Retry"), or NULL */
    int busy, percent;          /* downloading: a bar on the cover */
    int grey;                   /* not made for this device (bm_compat_classify): a faded cover and a "Not here" pill;
                                 * the caller asks before it starts, with the reason in the question */
} menu_item_t;

/* the covers: squares (2026-10-04, the bm Suite's icons; 128x80 before),
 * MENU_COLS a row on the Pi's 640 pixels, 3 on the RGB30's 360 */
#define MENU_CARD 88
#define MENU_COLS 6
/* covers per row of the open menu: 4 on the Pi, 2 on the RGB30 */
int menu_ui_cols(void);

#define MENU_BAR_PARTS 7        /* parts of a MENU_ROW_BAR */

/* A row of a panel (a submenu over the grid). */
enum {
    MENU_ROW_ACTION,            /* A does something */
    MENU_ROW_SUB,               /* A opens another panel: "label  value >" */
    MENU_ROW_CHOICE,            /* left/right (or A) change the value: "< value >" */
    MENU_ROW_INFO,              /* only shows a value */
    MENU_ROW_BAR,               /* only draws `bar`: the label on the left, up to MENU_BAR_PARTS parts of the width on the right */
};

typedef struct {
    const char *label;
    const char *value;          /* on the right, or NULL */
    int kind;
    const uint16_t *bar;        /* MENU_ROW_BAR: MENU_BAR_PARTS parts of the bar in 1/1000, in order (the rest is the background) */
    uint32_t swatch;            /* 0xRRGGBB square before the label, 0: none */
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
    const char *const *subtabs; /* Market sub-pages ("Games", "Apps"), or NULL */
    int nsubtabs, subtab;
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
    const char *ok_btn, *back_btn;  /* MENU_PROMPTS_RGB30: the face buttons ("A".."Y") of OK and Cancel; NULL: B and Y */
    int confirm_b;              /* MENU_PROMPTS_PAD, _RGB30: confirm is B, back A (the RGB30) */
    int no_monitor;             /* no monitor to go to (the RGB30): no hint for it */
    int keys_help;              /* F12 is held: the keys over everything (the system's, the menu's) */
    const menu_panel_t *panel;  /* a submenu over the grid, or NULL */
    const menu_page_t *page;    /* Settings: a page instead of the grid, or NULL */
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
    const char *y_label;        /* Y in the footer (e.g. "Apps" / "Games" in Market), or NULL */
    const char *refresh_label;  /* "Refresh" in Market, or NULL */
    int refresh_enabled;        /* 1 if Refresh can be pressed, 0 if greyed out */
    /* called while the frame waits for its time, until timer_ticks() is
     * `until` (the Market's downloads), or NULL */
    void (*idle)(uint32_t until);
} menu_view_t;

/* What is under a point of the last frame (M32: the pointer), the
 * topmost thing: a cover (index; full = not cut by the edges of the grid),
 * a tab (index), Settings, a row of the panel (index in its rows), a
 * button of the hints (index 'A', 'B', 'X' or 'Y'), the panel or the
 * question elsewhere, a subtab of the Market (index), a section of
 * the Settings page (index), or nothing. */
enum { MENU_HIT_NONE, MENU_HIT_COVER, MENU_HIT_TAB, MENU_HIT_SETTINGS, MENU_HIT_ROW,
       MENU_HIT_BUTTON, MENU_HIT_PANEL, MENU_HIT_ASK, MENU_HIT_GROUP,
       MENU_HIT_SECTION };         /* a section of the Settings page while its rows have the focus */
typedef struct { int kind, index, full; } menu_hit_t;
menu_hit_t menu_ui_hit(int x, int y);

/* What the pointer (M32) asks of the menu in one frame, the same for the Pi's
 * menu (carts.c) and the RGB30's (rgb30/ui.c). The caller fills the "in"
 * fields and applies the "out" fields to its own state. */
typedef struct {
    /* in */
    int gfx;                    /* the graphic menu is on screen (else the motion is only dropped) */
    int ask;                    /* a question is open */
    int panel;                  /* a panel is open over the covers */
    int panel_rows;             /* its rows */
    int on_gear;                /* Settings is the tab: its sections can be clicked */
    int market;                 /* the Market list is shown: its pages and Refresh can be clicked */
    int covers;                 /* the covers of the list */
    int market_page;            /* the page of the Market shown now */
    /* out */
    int dy;                     /* the wheel: rows down (up is negative) */
    int action, back, opts, ybtn, refresh;
    int tabto;                  /* a tab clicked, -1 none */
    int settings;               /* the gear clicked */
    int panel_sel;              /* a row of the panel under the pointer, -1 none */
    int section;                /* a section of the Settings page clicked, -1 none */
    int cover_sel;              /* a cover under the pointer, -1 none */
    int page;                   /* a page of the Market clicked, -1 none */
} menu_ptr_t;
/* Reads the pointer (also when nothing shows it: the motion is dropped).
 * Returns 1 if it is shown and the out fields say what it did. */
int menu_ui_pointer(menu_ptr_t *io);

/* Switches the screen to the menu mode (it has the pointer); -1 if it
 * cannot (the caller keeps the text menu). */
int  menu_ui_open(framebuffer_t *fb);
/* One frame: draws and shows it, waits for the next frame time. */
void menu_ui_frame(framebuffer_t *fb, const menu_view_t *v);
/* 1 if the selection and the tab have not changed for `us` microseconds (also before the first change).
 * The background jobs start a new cover only then: while the user moves, they leave the CPU to the menu. */
int  menu_ui_still(uint32_t us);
/* Back to the console mode. */
void menu_ui_close(framebuffer_t *fb);
/* The same, the console left suspended: a game's loading screen takes the
 * screen at once, without the log in between */
void menu_ui_close_quiet(framebuffer_t *fb);

/* A cover for cartridges without one: the title on a coloured label. */
int  menu_make_cover(g16_sheet_t *s, const char *title);
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
