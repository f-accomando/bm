/*
 * Shell SDK: the animations of the Sprite tab. An animation is 1 to 8 frames of a block of 8x8 tiles (a sprite
 * made of hardware sprites). Each cell of a frame says a tile of the sheet and how it is flipped, so a tile can
 * show many times, mirrored. The code is the truth: the block between ;@anim and ;@end (//@anim ... //@end in
 * bm C), one list anim_NAME for each:
 *
 *     width, height (in tiles), frames, speed (screen frames a frame), then for each frame, row by row,
 *     tile, attribute for each cell. The attribute: the palette (0 to 3), $40 mirrored, $80 upside down,
 *     $10 an empty cell.
 *
 * The pixels live in the sheet; the tab edits them in a workspace and puts them back with sdk_anim_plan and
 * sdk_anim_place. Pure model: the tests run on the PC.
 */
#ifndef SHELL_SDK_ANIM_H
#define SHELL_SDK_ANIM_H

#include <stddef.h>
#include <stdint.h>

#define SDK_ANIM_MAX 16
#define SDK_ANIM_FRAMES 8
#define SDK_ANIM_SIDE 4                 /* tiles, at most, across and down */
#define SDK_ANIM_CELLS (SDK_ANIM_SIDE * SDK_ANIM_SIDE)
#define SDK_ANIM_PIXELS (SDK_ANIM_SIDE * 8)
#define SDK_ANIM_UNIQUE (SDK_ANIM_FRAMES * SDK_ANIM_CELLS)
#define SDK_ANIM_TILES 256              /* a sprite names a tile 0 to 255 */
#define SDK_ANIM_NAME 12                /* with the NUL */

enum { SDK_ANIM_EMPTY = 0x10, SDK_ANIM_FLIP_X = 0x40, SDK_ANIM_FLIP_Y = 0x80 };

typedef struct {
    char name[SDK_ANIM_NAME];           /* without anim_ */
    uint8_t wide, high, frames, speed;
    uint8_t cell[SDK_ANIM_FRAMES][SDK_ANIM_CELLS][2];    /* tile, attribute; row by row */
    int line;                           /* of the source (0: not read from it) */
} sdk_anim_t;

typedef struct {
    int count;
    sdk_anim_t anim[SDK_ANIM_MAX];
} sdk_anims_t;

/* The pixels of the frames being edited: colour numbers, 0 transparent. */
typedef uint8_t sdk_anim_pixels_t[SDK_ANIM_FRAMES][SDK_ANIM_PIXELS][SDK_ANIM_PIXELS];

/* A new animation: 16x16, 4 frames, 8 screen frames each, empty cells. */
void sdk_anim_new(sdk_anim_t *a, const char *name);

/* Reads the block of a source (no block: none). Returns 0, or -1 with "line N: ..." in err. */
int sdk_anim_parse(const char *src, size_t len, int c, sdk_anims_t *out, char *err, size_t en);
/* The block as text with its marker lines; no animation: an empty block. Its length, or -1 when it does not fit. */
long sdk_anim_text(const sdk_anims_t *a, int c, char *out, size_t max);
/* Writes the block into the code (run as sdk_edit_replace). 0, or -1 with the reason in err. */
int sdk_anim_commit(const sdk_anims_t *a, int c, int run, char *err, size_t en);

/* The pixels of the frames from the sheet. format: the tiles are read as the sheet holds them. */
void sdk_anim_load(const sdk_anim_t *a, sdk_anim_pixels_t pix);

/* What the frames need: the tiles to write (each 64 pixels) and for each cell which one and how it is flipped.
 * reuse: cells that are the same, also mirrored or upside down, take one tile. */
typedef struct {
    int count;                                          /* tiles */
    uint8_t tile[SDK_ANIM_UNIQUE][64];
    int16_t use[SDK_ANIM_FRAMES][SDK_ANIM_CELLS];       /* the tile of the cell, -1: empty */
    uint8_t flip[SDK_ANIM_FRAMES][SDK_ANIM_CELLS];      /* SDK_ANIM_FLIP_X, _Y */
} sdk_anim_plan_t;

void sdk_anim_plan(const sdk_anim_t *a, sdk_anim_pixels_t pix, int reuse, sdk_anim_plan_t *p);

/* Where the tiles of a plan go: from the tile `start`, in the order of the sheet, into the empty cells and into
 * the tiles `own` (flags, SDK_ANIM_TILES; NULL: none). where[] gets p->count numbers. Returns how many found a
 * place: p->count when they all fit. */
int sdk_anim_spots(const sdk_anim_plan_t *p, int start, const uint8_t *own, int *where);

/* The tiles of an animation that no other one names: they are its own, and writing it again may use them. */
void sdk_anim_own(const sdk_anims_t *all, int which, uint8_t own[SDK_ANIM_TILES]);

/* Writes the tiles of the plan into the sheet at where[] (format: SDK_TILE_*) and the cells of the animation,
 * with the palette. */
void sdk_anim_place(sdk_anim_t *a, const sdk_anim_plan_t *p, const int *where, int format, int palette);

/* Takes the frames of an animation from the sheet: the block of tiles of frame f starts at the tile
 * start + f * width, as sprite sheets are drawn. Returns 0, or -1 when it passes the sheet. */
int sdk_anim_import(const sdk_anim_t *a, int start, sdk_anim_pixels_t pix);

/* Mirrors (x 1) or turns upside down the rectangle of a frame. */
void sdk_anim_flip(sdk_anim_pixels_t pix, int frame, int x0, int y0, int x1, int y1, int x);

int sdk_anim_name_ok(const char *name);

/* ---- the recall from the program ---- */

/* 1 when the source calls the animations: animate() in bm C (and it has no function of that name), anim_draw or
 * anim_tick in assembly. */
int sdk_anim_called(const char *src, size_t len, int c);
/* animate(sprite, x, y, anim_NAME) in bm C, put after a program that calls it: it draws the frame of now. */
const char *sdk_anim_prelude(int snes);

/* The source of a NES assembly project with the routines anim_draw and anim_tick after the block. */
typedef struct {
    char *text;             /* from malloc; NULL: the source as it is */
    size_t len;
    int first, lines;       /* the routines: their first line and how many */
} sdk_anim_src_t;

/* Returns 0, or -1 with the message in err. */
int sdk_anim_expand(const char *src, size_t len, sdk_anim_src_t *s, char *err, size_t en);
/* The assembler's message for the expanded source: its line of the source. */
void sdk_anim_error_line(const sdk_anim_src_t *s, char *err, size_t en);

#endif /* SHELL_SDK_ANIM_H */
