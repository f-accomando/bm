/*
 * Shell SDK, Sprite tab: the sprite sheet, 16 x 32 tiles where the tiles copied from a ROM are pasted and the
 * pixel canvas (shell_sdk_canvas.h) paints, and the clipboard of tiles. A tile keeps its 64 colour numbers and
 * the format they were read with. Pure model: the tests run on the PC.
 */
#ifndef SHELL_SDK_SHEET_H
#define SHELL_SDK_SHEET_H

#include <stddef.h>
#include <stdint.h>

#define SDK_SHEET_W 16                 /* tiles in a row: 128 pixels, as the tables of the NES */
#define SDK_SHEET_H 32
#define SDK_CLIP_TILES (SDK_SHEET_W * SDK_SHEET_H)    /* the most the clipboard holds */

typedef struct {
    uint8_t used;                       /* 0: an empty cell */
    uint8_t fmt;                        /* SDK_TILE_* */
    uint8_t pix[64];                    /* colour numbers, rows of 8 */
} sdk_tile_t;

/* An empty sheet and an empty clipboard. */
void sdk_sheet_reset(void);
int sdk_sheet_empty(void);
/* The tile at row r, column c (both inside the sheet). */
const sdk_tile_t *sdk_sheet_tile(int r, int c);
/* Puts a tile at row r, column c (nothing outside the sheet). */
void sdk_sheet_set(int r, int c, const sdk_tile_t *t);

/* A new block of w x h tiles in the clipboard, all empty; the rows that do not fit are cut. Fill it with
 * sdk_clip_cell (NULL outside the block). */
void sdk_clip_start(int w, int h);
sdk_tile_t *sdk_clip_cell(int r, int c);
/* The size of the block. Returns 0 when the clipboard is empty. */
int sdk_clip_size(int *w, int *h);
/* A number that changes at each new block: the canvas knows which of the two copies is the last. */
unsigned sdk_clip_stamp(void);

/* Pastes the block with its first tile at row r, column c; the part outside the sheet is left out. Returns the
 * tiles placed. */
int sdk_sheet_paste(int r, int c);
/* The rectangle from (r1, c1) to (r2, c2), both included: copies it to the clipboard, or empties it. */
void sdk_sheet_copy(int r1, int c1, int r2, int c2);
void sdk_sheet_clear(int r1, int c1, int r2, int c2);

/* The part of a ROM of size bytes that an import takes, with tiles of tile bytes. chr_from, chr_len: its CHR ROM
 * (chr_len 0: it has none); view: the first tile that the view shows. A CHR ROM that the sheet holds is taken
 * whole; else the tiles from view, to the end of the CHR ROM or of the file, at most SDK_CLIP_TILES. Returns
 * where it starts; *count gets the tiles. */
size_t sdk_sheet_part(size_t size, size_t chr_from, size_t chr_len, size_t view, int tile, int *count);
/* The sheet becomes count tiles of data from the offset from, in their order (16 a row); a tile of colour 0
 * only is an empty cell, the cells after the last tile are empty. */
void sdk_sheet_import(const uint8_t *data, size_t size, size_t from, int fmt, int count);

#endif /* SHELL_SDK_SHEET_H */
