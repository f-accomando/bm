/*
 * The loading screen of a game (the user's request, 2026-10-04): instead of
 * the log, a retro pixel animation (the bm logo as pixel art falling into
 * place, a jingle when it lands, a circle of dots turning) drawn on a 160x90
 * canvas made bigger on whatever screen there is, until the game is loaded
 * (its file, its assets, its _init); then the game starts. The system's: the
 * same for every game and tool, no title (the user's decision). At least
 * the intro (the logo, the jingle) is shown, as on the old consoles.
 *
 * The menu begins it (loading_begin) before it reads the file; the runtime
 * ticks it while it loads (loading_tick: the SD card, the assets, the Lua
 * hook), moves it to the game's page once the game's mode is set
 * (loading_page) and ends it before the first frame (loading_end). Without
 * a begin nothing is drawn (a game tried from a tool, bmhost, the tests
 * that run bm_run directly). game_intro=0 in bm/config.txt turns it off.
 */
#ifndef LOADING_H
#define LOADING_H

#include <stdint.h>
#include "drivers/fb.h"
#include "gfx16.h"

/* the bm logo as pixel art (scripts/mklogo.py writes loading_logo.c);
 * LOADING_KEY pixels are see-through */
#define LOADING_KEY 0xF81Fu
extern const int loading_logo_w, loading_logo_h;
extern const uint16_t loading_logo[];

/* Starts it on the console's page (32 bits a pixel; the console is
 * suspended). */
void loading_begin(framebuffer_t *fb);
int  loading_active(void);
/* From now on on the game's page, shown with bm_video_present. */
void loading_page(framebuffer_t *fb, g16_t *page);
/* A frame when it is time (about 30 a second), the jingle's notes on time;
 * cheap otherwise: call it often while loading. */
void loading_tick(void);
/* The game is loaded: waits for the end of the intro, then it is over. */
void loading_end(void);
/* Over at once (an error): no waiting. */
void loading_stop(void);

#endif
