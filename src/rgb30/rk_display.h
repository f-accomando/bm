/* RGB30 display pieces (rk_display.c drives them in order). */
#ifndef RK_DISPLAY_H
#define RK_DISPLAY_H

#include <stdint.h>

/* DSI0 + D-PHY + ST7703 panel, with the VOP already streaming; a short
 * report goes to log. 0 ok, -1 panel commands failed, -2 no PHY lock. */
int rk_dsi_init(char *log, unsigned size);
/* the panel off before a restart: display off and sleep in if the link
 * is up, then reset, supply off and 300 ms to drain */
void rk_dsi_off(int link_up);
/* A step of the screen's start, while booting: a "display: ..." line, and
 * bm/bootlog.txt written again (main.c), so a black start says how far
 * it went. */
void rgb30_display_stage(const char *what);
/* Before a restart or power off: this run's log to bm/lastrun.txt (main.c;
 * skipped from an interrupt or with an SD write half way) */
void rgb30_save_lastrun(void);

#endif
