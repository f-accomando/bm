/*
 * BareMetal UI (M27): what the home menu offers besides the cartridges.
 * The development tools of the Dev tab, and the panels (submenus) of the
 * settings. The menu itself, with the cartridges and their options, is in
 * carts.c; it draws the panels built here with menu_ui.
 */
#ifndef HOME_H
#define HOME_H

#include "menu_ui.h"
#include "drivers/fb.h"
#include "bm/gfx16.h"

#define HOME_ROWS_MAX 16

/* A panel being built: rows with their own text, what each one does, and
 * a line of help for each. */
typedef struct {
    char title[64];
    menu_row_t rows[HOME_ROWS_MAX];
    int ids[HOME_ROWS_MAX];
    const char *help[HOME_ROWS_MAX];
    char label[HOME_ROWS_MAX][32];
    char value[HOME_ROWS_MAX][40];
    int n;
} home_panel_t;

/* Adds a row; `fmt` makes the value (NULL: none). */
void home_row(home_panel_t *p, int kind, int id, const char *label, const char *help,
              const char *fmt, ...) __attribute__((format(printf, 6, 7)));

/* The panels. HOME_CART (a cartridge's options) is built by carts.c,
 * HOME_MARKET (a game of the Market) by market.c. */
enum { HOME_SETTINGS = 1, HOME_CONTROLLERS, HOME_WIFI, HOME_SYSTEM, HOME_CART, HOME_MARKET };

/* What a row asks of the menu. */
enum {
    HOME_STAY,                  /* done (a value changed): stay on the panel */
    HOME_OPEN,                  /* open panel `panel` over this one */
    HOME_BACK,                  /* close this panel */
    HOME_ASK,                   /* a question; on yes the row runs again with how = HOME_YES */
    HOME_TEXT,                  /* run `text` on the text console, then back to the menu */
    HOME_MONITOR,               /* leave the menu for the monitor */
};

#define HOME_YES 2              /* `how` after a question was answered yes */

typedef struct {
    int what;
    int panel;                  /* HOME_OPEN */
    char ask[64], ask_detail[64], ask_yes[16];      /* HOME_ASK */
    void (*text)(framebuffer_t *fb);                /* HOME_TEXT */
    int wait;                   /* HOME_TEXT: then "A: back to the menu" */
    char note[96];              /* a line for the menu's footer, if not empty */
} home_do_t;

/* Builds settings panel `id`. */
void home_panel(int id, home_panel_t *p);
/* Row `row_id` of panel `id` was chosen: how = 0 (A), -1 / +1 (left, right
 * on a choice), HOME_YES (the question was answered yes). */
void home_act(int id, int row_id, int how, home_do_t *d);

/* The tools of the Dev tab (after the SDK), with their covers. */
void home_init(void);
int  home_tools(void);
const char *home_tool_title(int i);
const char *home_tool_about(int i);
const g16_sheet_t *home_tool_cover(int i);
void home_tool_start(int i, home_do_t *d);

/* After a tool on the text console: "A: back to the menu", from any
 * controller, the keyboard or the serial port. */
void home_wait_back(void);

/* the development assistant (M30), the Dev tab's Assistant */
void home_assistant(framebuffer_t *fb);

#endif
