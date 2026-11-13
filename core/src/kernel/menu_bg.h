/*
 * The menu's background (menu_ui.c): the selected cover shrunk to 16x10,
 * stretched back to the whole screen with bilinear filtering, darkened
 * towards the bottom and dithered to RGB565.
 *
 * It is built in bands of rows, a few per frame (menu_bg_step). One call
 * for the whole screen was a long frame on the Pi Zero W each time the
 * selection changed, and the mouse pointer jumped when it moved over the
 * covers (2026-10-23). No integers or floats outside this file: it runs on
 * the PC in tests/ui/test_menu_bg.c.
 */
#ifndef MENU_BG_H
#define MENU_BG_H

#include <stdint.h>

#define MENU_BG_MAX_W 640

typedef struct {
    int32_t lo[10][16][3];          /* the cover in 16x10 blocks, 8.8 fixed point */
    uint16_t fx[MENU_BG_MAX_W];     /* per column: the weight of the right block, 0..256 */
    uint16_t edge[MENU_BG_MAX_W];   /* per column: the darkening at the sides, 8.8 */
    uint8_t ia[MENU_BG_MAX_W], ib[MENU_BG_MAX_W];
    int sw, sh;                     /* the picture, sw <= MENU_BG_MAX_W */
    int row;                        /* the next row to make; sh: done */
} menu_bg_t;

/* Starts a picture of sw x sh from an RGB565 cover (w x h; NULL or smaller
 * than 16x10: the dark blue of an empty cover). Nothing is drawn yet. */
void menu_bg_begin(menu_bg_t *b, const uint16_t *cover, int w, int h, int sw, int sh);

/* Makes the next `rows` rows of out (sw wide), at most; returns 1 when the
 * picture is whole. The cover is not read again: it may go away. */
int menu_bg_step(menu_bg_t *b, uint16_t *out, int rows);

#endif
