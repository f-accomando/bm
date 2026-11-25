/*
 * Shell SDK, Sprite tab: the canvas, a sheet of 16 x 32 tiles where the tiles copied from a ROM are pasted, and
 * the clipboard of tiles. A tile keeps its 64 colour numbers and the format they were read with. Pure model:
 * the tests run on the PC.
 */
#ifndef SHELL_SDK_CANVAS_H
#define SHELL_SDK_CANVAS_H

#include <stdint.h>

#define SDK_CANVAS_W 16                 /* tiles in a row: 128 pixels, as the tables of the NES */
#define SDK_CANVAS_H 32
#define SDK_CLIP_TILES (SDK_CANVAS_W * SDK_CANVAS_H)    /* the most the clipboard holds */

typedef struct {
    uint8_t used;                       /* 0: an empty cell */
    uint8_t fmt;                        /* SDK_TILE_* */
    uint8_t pix[64];                    /* colour numbers, rows of 8 */
} sdk_tile_t;

/* An empty canvas and an empty clipboard. */
void sdk_canvas_reset(void);
int sdk_canvas_empty(void);
/* The tile at row r, column c (both inside the canvas). */
const sdk_tile_t *sdk_canvas_tile(int r, int c);

/* A new block of w x h tiles in the clipboard, all empty; the rows that do not fit are cut. Fill it with
 * sdk_clip_cell (NULL outside the block). */
void sdk_clip_start(int w, int h);
sdk_tile_t *sdk_clip_cell(int r, int c);
/* The size of the block. Returns 0 when the clipboard is empty. */
int sdk_clip_size(int *w, int *h);

/* Pastes the block with its first tile at row r, column c; the part outside the canvas is left out. Returns the
 * tiles placed. */
int sdk_canvas_paste(int r, int c);
/* The rectangle from (r1, c1) to (r2, c2), both included: copies it to the clipboard, or empties it. */
void sdk_canvas_copy(int r1, int c1, int r2, int c2);
void sdk_canvas_clear(int r1, int c1, int r2, int c2);

#endif /* SHELL_SDK_CANVAS_H */
