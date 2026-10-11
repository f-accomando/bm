/*
 * Publishing a game of the SD card to the Market from its options (M25,
 * step 6): a panel (the id, the version, the license to choose, the
 * token), then a pull request to the market's repository (net/github.c)
 * in a fiber of the menu: the panel's Send row says how far it is, then
 * the pull request (or why not). The token is github_token in
 * bm/config.txt (a GitHub personal token that can write public
 * repositories); the market is market_repo (default f-accomando/bm-market).
 */
#ifndef PUBLISH_H
#define PUBLISH_H

#include <stdint.h>

#include "home.h"

/* The game the panel is about: its file, title and author (while one is
 * being sent, that one stays). */
void publish_setup(const char *path, const char *title, const char *author);
void publish_panel(home_panel_t *p);
void publish_act(int row, int how, home_do_t *d);
/* The sending, in the menu's free time; the title of the game being sent,
 * NULL when none (no application starts meanwhile). */
void publish_tick(uint32_t until);
const char *publish_busy(void);

#endif
