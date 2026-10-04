/*
 * The Market (M25): the games of the catalog (net/catalog.c) for the
 * Market tab of the menu (carts.c), their covers and their downloads.
 *
 * Everything that takes time (the SD card cache, the catalog, the covers,
 * a download) runs in a fiber, one job at a time, and only while the tab
 * is shown: market_tick gives it what is left of each menu frame, so the
 * menu stays at 60 fps; market_set_active(0) cancels the job in flight.
 * Until things arrive the grid shows placeholders.
 *
 * The catalog comes from market_url in bm/config.txt (default
 * https://f-accomando.github.io/bm-market/), or from a folder of the SD
 * card with the same files (market_url=sd:/market/): either way it must
 * be signed with the Market key (keys/market-pub.pem, built in, or
 * bm/market.pem on the SD card for a market of one's own) and every file
 * must have the size and SHA-256 the catalog says. Games go into /carts;
 * the cache and the list of installed games into /bm/market.
 */
#ifndef MARKET_H
#define MARKET_H

#include <stddef.h>
#include <stdint.h>

#include "home.h"
#include "menu_ui.h"

/* The Market tab is shown (1) or not (0: the job in flight is cancelled). */
void market_set_active(int on);
/* Work until timer_ticks() reaches `until` (the menu's idle time). */
void market_tick(uint32_t until);
/* The highlighted game: its cover and the ones around it come first. */
void market_select(int i);

/* The grid: the games, or placeholders while the catalog loads. */
int  market_items(menu_item_t *items, int max);
/* A line instead of a game's title ("Loading the Market..."), or NULL. */
const char *market_banner(void);
/* The footer line: the catalog, the last download. */
const char *market_status(void);
/* The details line of game i (author, version, license, size). */
void market_details(int i, char *buf, size_t n);

/* What A does on game i. */
enum { MARKET_NONE, MARKET_GET, MARKET_PLAY, MARKET_UPDATE, MARKET_BUSY };
int  market_action(int i);
/* The label of A for game i ("Get", "Play"...), or NULL. */
const char *market_action_label(int i);
/* A question before downloading game i: fills d (HOME_ASK). */
void market_ask(int i, home_do_t *d);
/* Downloads game i (queued: it starts in the next frames). */
void market_get(int i);
/* The installed file of game i, "" if it is not on the SD card. */
const char *market_path(int i);

/* The options of game i (X): a panel like the settings. */
enum { M_GET = 300, M_PLAY, M_UPDATE, M_AUTHOR, M_VERSION, M_LICENSE, M_SIZE, M_FILE, M_DELETE };
void market_panel(int i, home_panel_t *p);
void market_act(int i, int row, int how, home_do_t *d);

/* The catalog's record of the game with this title and author (case
 * ignored), if the catalog is in memory: its id, version, license and
 * about (publish.c updates the same folder). 0, or -1. */
int  market_lookup(const char *title, const char *author, char *id, size_t idn, char *version,
                   size_t vn, char *license, size_t ln, char *about, size_t an);

/* Games between consoles on the home network (M24, net/lan.c): on while
 * the Market tab or the "Send to a nearby console" panel is shown, called
 * every frame (it also keeps a game that arrived). */
void market_lan(int on);
/* A nearby console offers a game: 1, the question and its detail (whether
 * the Market's catalog has these very bytes). */
int  market_offer(char *q, size_t qn, char *detail, size_t dn);
void market_offer_answer(int yes);

/* The Send panel of a game of the SD card: the consoles heard nearby. */
enum { S_THIS = 500, S_NONE, S_PEER };          /* S_PEER + i: the i-th console */
void market_send_setup(const char *path, const char *title, const char *author);
void market_send_panel(home_panel_t *p);
void market_send_act(int row, int how, home_do_t *d);

/* 1 once after a game was installed or deleted: the menu reads the SD card
 * again (then market_carts_changed). */
int  market_take_changed(void);
/* The cartridges on the SD card changed (carts.c rescanned them). */
void market_carts_changed(void);

#endif
