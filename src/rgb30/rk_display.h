/* RGB30 display pieces (rk_display.c drives them in order). */
#ifndef RK_DISPLAY_H
#define RK_DISPLAY_H

#include <stdint.h>

/* DSI0 + D-PHY + ST7703 panel, with the VOP already streaming; a short
 * report goes to log. 0 ok, -1 panel commands failed, -2 no PHY lock. */
int rk_dsi_init(char *log, unsigned size);

#endif
