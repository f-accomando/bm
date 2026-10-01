/*
 * Publishing a game of the SD card to the Market from its options (M25,
 * step 6): a panel (the id, the version, the license to choose, the
 * token), then a pull request to the market's repository on the text
 * console (net/github.c). The token is github_token in bm/config.txt (a
 * GitHub personal token that can write public repositories); the market
 * is market_repo (default f-accomando/bm-market).
 */
#ifndef PUBLISH_H
#define PUBLISH_H

#include "home.h"

/* The game the panel is about: its file, title and author. */
void publish_setup(const char *path, const char *title, const char *author);
void publish_panel(home_panel_t *p);
void publish_act(int row, int how, home_do_t *d);

#endif
