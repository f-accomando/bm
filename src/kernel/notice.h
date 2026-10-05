/*
 * The system's notices, in a box over whatever is on the screen (the menu,
 * a game; the text console gets lines instead): a new kernel arriving over
 * the network, with its progress, then the 3 s before the restart, counted
 * down (the user's request, 2026-10-04); and short ones, gone by
 * themselves (the RGB30's volume keys).
 */
#ifndef NOTICE_H
#define NOTICE_H

#include <stdint.h>

#define NOTICE_LEN 64

/* 1 while there is one: its title and line (NOTICE_LEN bytes each), and
 * its progress, 0..1000 for a bar or -1 */
int notice_now(char *title, char *detail, int *progress);

/* A notice for ms milliseconds (the last one wins; a kernel arriving
 * comes first): its title, line and bar (0..1000, or -1). */
void notice_flash(const char *title, const char *detail, int progress, uint32_t ms);

#endif
