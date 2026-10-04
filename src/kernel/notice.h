/*
 * The system's notices, in a box over whatever is on the screen (the menu,
 * a game; the text console gets lines instead): a new kernel arriving over
 * the network, with its progress, then the 3 s before the restart, counted
 * down (the user's request, 2026-10-04).
 */
#ifndef NOTICE_H
#define NOTICE_H

#define NOTICE_LEN 64

/* 1 while there is one: its title and line (NOTICE_LEN bytes each), and
 * its progress, 0..1000 for a bar or -1 */
int notice_now(char *title, char *detail, int *progress);

#endif
