/*
 * Shell SDK, Code tab: the bytes of a ROM read as 8x8 tiles. The decoder and the scroll, no drawing (the
 * tests run on the PC).
 */
#ifndef SHELL_SDK_TILES_H
#define SHELL_SDK_TILES_H

#include <stddef.h>
#include <stdint.h>

#include "shell_sdk_rom.h"

#define SDK_TILES_ROW     16        /* tiles in a row of the view when a ROM opens */
#define SDK_TILES_ROW_MAX 32

/* NES: 8 bytes of bit 0, then 8 bytes of bit 1. SNES: the bits in pairs, two bytes per line (4 bpp: the
 * lines of bits 0 and 1, then those of bits 2 and 3). */
enum { SDK_TILE_NES, SDK_TILE_SNES2, SDK_TILE_SNES4, SDK_TILE_SNES8 };

int sdk_tile_bytes(int fmt);                    /* 16, 16, 32, 64 */
const char *sdk_tile_name(int fmt);             /* "NES 2bpp", "4bpp" ... */
int sdk_tile_colors(int fmt);                   /* 4, 4, 16, 256 */
/* The format after fmt: a SNES ROM turns 4 bpp, 2 bpp, 8 bpp; a NES ROM has one. */
int sdk_tile_next(int fmt);

/* The 64 colour numbers (rows of 8) of the tile at off. A byte past the end of the data is 0. */
void sdk_tile_decode(const uint8_t *data, size_t size, size_t off, int fmt, uint8_t pix[64]);

/* Where the view of a ROM starts: the CHR ROM of a NES file (chr is set to 1), else the PRG; a SNES file
 * after its copier header. fmt gets the format to read it with. */
size_t sdk_tiles_start(const uint8_t *data, size_t size, const sdk_rom_t *info, int *fmt, int *chr);

/* The tile of the cell at row r, column c of a view with per tiles a row, counted from the first tile shown.
 * tall: the order of 8x16 sprites, where a tile and the one after it are above each other. */
int sdk_tiles_cell(int r, int c, int per, int tall);

enum { SDK_TILES_UP, SDK_TILES_DOWN, SDK_TILES_PGUP, SDK_TILES_PGDN, SDK_TILES_LEFT, SDK_TILES_RIGHT,
       SDK_TILES_END, SDK_TILES_BACK, SDK_TILES_NEXT };

/* The new offset of the view after a move: a row, a page of rows rows, a byte (to find where the tiles
 * start), a tile (to put the first one of a picture at the left) or the last page. group: the tiles of a row
 * (twice the tiles a row in the 8x16 order, where a row is two tiles high). */
size_t sdk_tiles_move(size_t off, size_t size, int fmt, int group, int rows, int move);

#endif /* SHELL_SDK_TILES_H */
