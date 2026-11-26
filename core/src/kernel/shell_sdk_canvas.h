/*
 * Shell SDK, Sprite tab: the canvas, the pixels of one tile (8x8) or of four (16x16) of the sprite sheet
 * (shell_sdk_sheet.h). It paints straight into the sheet. It has a cursor, a selection, the colour of the pen
 * and a clipboard of pixels. Pure model: the tests run on the PC.
 */
#ifndef SHELL_SDK_CANVAS_H
#define SHELL_SDK_CANVAS_H

#include <stdint.h>

typedef struct {
    int open;                   /* a tile of the sheet was chosen */
    int row, col;               /* the first tile of the block in the sheet */
    int size;                   /* 8 or 16 pixels a side */
    int x, y;                   /* the pixel of the cursor */
    int sel, ax, ay;            /* a selection, from the pixel (ax, ay) to the cursor */
    int colour;                 /* the colour number of the pen, 1 or more (0 is the transparent one) */
} sdk_canvas_t;

/* Closed, 8x8, colour 1, nothing copied. */
void sdk_canvas_reset(void);
const sdk_canvas_t *sdk_canvas(void);
/* The format of the tiles of the project (SDK_TILE_*), or -1: each tile keeps its own, an empty one is 4bpp. */
void sdk_canvas_format(int fmt);
/* Chooses the tile at row r, column c of the sheet, with the size of the canvas (8 or 16; another value keeps
 * the size). A block of 16x16 stays inside the sheet. */
void sdk_canvas_open(int r, int c, int size);
/* 8 or 16 pixels a side; a chosen block stays inside the sheet. */
void sdk_canvas_size(int size);
/* The colour numbers the canvas paints with, 0 included: 4, 16 or 256. */
int sdk_canvas_colours(void);
/* The colour number of a pixel (0 outside the canvas or with no tile chosen). */
int sdk_canvas_pixel(int x, int y);
/* The next or the previous colour of the pen: 1 to sdk_canvas_colours() - 1, round. */
void sdk_canvas_colour_step(int step);
/* The cursor by dx, dy pixels, kept inside; shift: the pixels from where it started are selected. */
void sdk_canvas_move(int dx, int dy, int shift);
void sdk_canvas_select_all(void);
/* The selection, both pixels included; without one, the pixel of the cursor. Returns 1 when there is one. */
int sdk_canvas_rect(int *x1, int *y1, int *x2, int *y2);
/* Paints the selection (or the pixel of the cursor) with a colour number, 0 for transparent. cursor_only: the
 * pixel of the cursor, as when the key is held and the cursor moves. Returns the pixels that changed. */
int sdk_canvas_paint(int colour, int cursor_only);
/* Copy: the pixels of the selection; with no selection the tiles of the canvas, into the clipboard of tiles.
 * Returns the pixels copied, or 0 for tiles. */
int sdk_canvas_copy(void);
/* Empties what a copy takes: the selection, or the whole canvas. Returns the pixels that changed. */
int sdk_canvas_erase_copied(void);
/* 1 when the last copy was one of pixels (so it pastes here only). */
int sdk_canvas_has_pixels(void);
/* Pastes the last copy: pixels from the cursor (or the start of the selection), tiles on the tiles of the
 * canvas. The part outside is left out. *tiles: it was a copy of tiles. Returns the pixels or tiles placed,
 * -1 when nothing was copied. */
int sdk_canvas_paste(int *tiles);

#endif /* SHELL_SDK_CANVAS_H */
