/*
 * The boot splash (the user's request, 2026-10-04): the bm logo on its dark
 * background while the kernel starts; what it prints still goes to the
 * console (suspended: its text kept for the monitor and the console pages),
 * to the serial line and to the log (Settings > System > Log since boot).
 */
#ifndef SPLASH_H
#define SPLASH_H

#include <stdint.h>
#include "drivers/fb.h"

/* the logo, RGB565 (scripts/mklogo.py writes logo_data.c) */
extern const int bm_logo_w, bm_logo_h;
extern const uint32_t bm_logo_bg;           /* 0xRRGGBB, the screen around it */
extern const uint16_t bm_logo[];

/* Suspends the console and draws the logo, the version under it, on the
 * page shown. The menu takes the screen over when it opens. */
void splash_show(framebuffer_t *fb, const char *version);

#endif
