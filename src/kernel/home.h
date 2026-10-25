/*
 * BareMetal UI (M27): what the home menu offers besides the cartridges.
 * The development tools of the Dev tab (home.c), and the panels (submenus)
 * of the settings (settings.c, also the RGB30's). The menu itself, with the cartridges and their options, is in
 * carts.c; it draws the panels built here with menu_ui.
 */
#ifndef HOME_H
#define HOME_H

#include "menu_ui.h"
#include "crt.h"
#include "drivers/fb.h"
#include "bm/gfx16.h"

#define HOME_ROWS_MAX 28

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
 * HOME_MARKET (a game of the Market) and HOME_SEND (to a nearby console)
 * by market.c, HOME_PUBLISH (sending a game to the Market) by publish.c. */
enum { HOME_SETTINGS = 1, HOME_CONTROLLERS, HOME_WIFI, HOME_SYSTEM, HOME_CART, HOME_GRAPHICS, HOME_MARKET,
       HOME_PUBLISH, HOME_SEND, HOME_UPDATES, HOME_REPORTS, HOME_UTILITIES, HOME_SDMAP, HOME_SDFILES,
       HOME_CONTROLS, HOME_CTL_SCROLLER, HOME_CTL_TOPDOWN, HOME_CTL_STICK, HOME_CTL_SYSTEM, HOME_SOUND };

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
    int own;                    /* HOME_TEXT: it draws its own screen, not the console (the RGB30) */
    char note[96];              /* what the row did, if not empty: a line for the console log */
    int fail;                   /* the row failed: `note` is the reason, and the menu shows it in the notice box */
} home_do_t;

/* The result of a row, after home_act (or the like): `note` goes to the
 * console log; a failure goes to the notice box too (notice_fail). The
 * menu's bottom bar shows the key chips only. */
void home_report(const home_do_t *d);

/* Settings > Controllers > Button icons: the DS4's face buttons in the
 * hints white (0) or in their colours (1); kept in config.txt. */
int  home_prompts_colour(void);

/* CRT filter settings and scanline effect: see crt.h */
void gpio_lcd_power(int on);

/* Builds settings panel `id` (settings.c: the same tree on the Pi and the
 * RGB30: Controllers, WiFi and network, Screen, Sound, Updates, Reports,
 * System). */
void home_panel(int id, home_panel_t *p);
/* The panel a row of Settings opens (0: none): the page shows it beside
 * the sections before it is opened. */
int  home_sub_panel(int row_id);
/* Row `row_id` of panel `id` was chosen: how = 0 (A), -1 / +1 (left, right
 * on a choice), HOME_YES (the question was answered yes). */
void home_act(int id, int row_id, int how, home_do_t *d);

/* The utilities (benchmarks, tests, configuration controls: type "utility", D09) of the running kernel, listed by
 * Settings > System > Utilities. The Pi defines them in home.c, the RGB30 in ui.c. */
int  home_utils(void);                                  /* how many */
const char *home_util_title(int i);
const char *home_util_about(int i);                     /* one line of help */
int  home_util_group(int i);                            /* 1: shown in Screen, 0: in System > Utilities */
void home_util_start(int i, home_do_t *d);              /* fills d as home_act does */

/* What differs between the consoles in Settings: a small function for each, defined in home.c (the Pi) and in
 * rgb30/ui.c (the RGB30), so settings.c has one text for both. */
int  home_wireless(void);                       /* the board has WiFi (else an Ethernet port) */
void home_restart(void);                        /* restarts the console (the WiFi is stopped already) */
void home_poweroff(void);                       /* shuts the console down */
const char *home_poweroff_help(void);           /* the help line of the row "Shut down" */
const char *home_poweroff_detail(void);         /* the second line of its question */
const char *home_test_help(void);               /* the help line of the row "Test the buttons" */
const char *home_connect_help(void);            /* the help line of the row "Connect to a network" */
int  home_has_battery(void);                    /* the console has a battery: the row "Low battery icon" (else "Test pattern") */
void home_controls_changed(void);               /* a button of Controls changed: the console reads them again */

/* The tools of the Dev tab (after the SDK), with their covers: the editors, the assistant, the monitor, Lua. */
void home_init(void);
int  home_tools(void);
const char *home_tool_title(int i);
const g16_sheet_t *home_tool_cover(int i);
void home_tool_start(int i, home_do_t *d);
int  home_tool_is_utility(int i);   /* tool i is a utility (not shown in Dev); home.c */

/* After a tool on the text console: "A: back to the menu", from any
 * controller, the keyboard or the serial port. */
void home_wait_back(void);

/* the development assistant (M30), the Dev tab's Assistant */
void home_assistant(framebuffer_t *fb);

/* everything printed since boot, in the pager (Dev > Log, Settings > System) */
void home_show_log(framebuffer_t *fb);

/* A tool's printout as a report (reports.h), for the Dev tab and the
 * monitor: _end saves and sends it, or drops it if the user stopped the
 * rendering test (Start+Select, Ctrl+Esc, PS: syskeys.h). */
void home_report_begin(const char *kind);
void home_report_end(void);

#endif
