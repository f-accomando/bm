/*
 * RGB565 drawing library for native bm cartridges (.bm).
 * All drawing is clipped to the clip rectangle and offset by the camera.
 * Colours are 16-bit RGB565 values (see g16_rgb).
 */
#ifndef GFX16_H
#define GFX16_H

#include <stdint.h>

#include "gfx/font.h"

#define G16_CELL 8              /* sprite sheet / map cell size in pixels */

typedef struct {
    int w, h;                   /* pixels, multiples of G16_CELL */
    uint16_t *px;               /* RGB565 */
    uint8_t *alpha;             /* 1 = opaque */
    uint8_t *cell_opaque;       /* per 8x8 cell: 1 if every pixel is opaque */
    uint32_t version;           /* changes with every g16_sheet_set / _update_cell, and is
                                 * new for every sheet allocated (copies, e.g. a GPU texture) */
} g16_sheet_t;

typedef struct {
    int w, h;                   /* cells */
    uint16_t *cells;            /* sprite index; 0 = empty */
} g16_map_t;

typedef struct {
    uint16_t *px;
    uint32_t stride;            /* pixels per row */
    int w, h;
    int cx0, cy0, cx1, cy1;     /* clip rectangle, x1/y1 exclusive */
    int cam_x, cam_y;
    const font_t *font;
} g16_t;

static inline uint16_t g16_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

static inline uint16_t g16_rgb24(uint32_t rgb)
{
    return g16_rgb(rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF);
}

/* RGB565 -> 0xRRGGBB with the low bits replicated */
uint32_t g16_to_rgb24(uint16_t c);

void g16_target(g16_t *g, uint16_t *px, uint32_t stride, int w, int h, const font_t *font);
void g16_clip(g16_t *g, int x, int y, int w, int h);    /* w or h <= 0: full screen */
void g16_camera(g16_t *g, int x, int y);

void g16_cls(g16_t *g, uint16_t c);
void g16_pset(g16_t *g, int x, int y, uint16_t c);
int  g16_pget(const g16_t *g, int x, int y);             /* -1 outside */
void g16_line(g16_t *g, int x0, int y0, int x1, int y1, uint16_t c);
void g16_rect(g16_t *g, int x, int y, int w, int h, uint16_t c);
void g16_rectfill(g16_t *g, int x, int y, int w, int h, uint16_t c);
void g16_circ(g16_t *g, int cx, int cy, int r, uint16_t c);
void g16_circfill(g16_t *g, int cx, int cy, int r, uint16_t c);

/* Sprite n (8x8 cell index, row-major in the sheet), wc x hc cells. */
void g16_spr(g16_t *g, const g16_sheet_t *s, int n, int x, int y,
             int wc, int hc, int flip_x, int flip_y);
/* Any rectangle of the sheet, unscaled. */
void g16_sspr(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
              int dx, int dy, int flip_x, int flip_y);
/* The same, `zoom` times bigger (or smaller, below 1), nearest pixel: it
 * covers round(sw * zoom) x round(sh * zoom) pixels of the screen. */
void g16_sspr_zoom(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                   int dx, int dy, int flip_x, int flip_y, float zoom);
/* Map cells [mx, mx+mw) x [my, my+mh) drawn at (x, y); cell 0 is skipped. */
void g16_map(g16_t *g, const g16_sheet_t *s, const g16_map_t *m,
             int mx, int my, int x, int y, int mw, int mh);
/* Text with the target's font (g->font), transparent background. Returns
 * the end x. */
int  g16_text(g16_t *g, int x, int y, const char *str, uint16_t c);
/* The same, every font pixel drawn as a scale x scale square. */
int  g16_text_scaled(g16_t *g, int x, int y, const char *str, uint16_t c, int scale);

/* Lighting: a grid of light values every 4 pixels (8.8 fixed point, 256 =
 * unchanged, up to 2x), filled with an ambient colour and soft round
 * lights, then multiplied into the picture (interpolated, dithered). */
typedef struct {
    int w, h;                   /* screen pixels */
    int nx, ny;                 /* nodes: w/4+1 x h/4+1 */
    uint16_t *rgb;              /* 3 per node */
} g16_light_t;

int  g16_light_init(g16_light_t *l, int w, int h);
void g16_light_free(g16_light_t *l);
void g16_light_clear(g16_light_t *l, uint32_t ambient_rgb);   /* 0xFFFFFF = unlit picture */
/* Adds a light at screen (x, y): full strength at the centre, zero at radius. */
void g16_light_add(g16_light_t *l, float x, float y, float radius, uint32_t rgb, float intensity);
void g16_light_apply(g16_t *g, const g16_light_t *l);

/* Lighting by levels, as in Dank Tomb (PICO-8): every pixel gets a light
 * level 0..levels-1 (the brightest lamp that reaches it: rings from the
 * lamp's level at the centre down to 0 at its radius, the ring edges
 * dithered 4x4), then each colour of the picture becomes the colour its
 * fade table gives for that level. Colours without a table are scaled by
 * the average of the tables at that level. */
#define G16_FADE_LEVELS 16
#define G16_FADE_COLOURS 255

typedef struct {
    int w, h;                   /* screen pixels */
    int levels;                 /* 2..G16_FADE_LEVELS */
    int ncol;                   /* colours with a table */
    uint8_t *lv;                /* w x h levels */
    uint8_t *index;             /* 65536: RGB565 -> its table, 255 = none */
    uint16_t *tab;              /* G16_FADE_LEVELS x 256: the colour at each level */
    uint16_t from[256];         /* the colour of each table */
    uint16_t mul[G16_FADE_LEVELS][3];   /* the others: r, g, b x 256 */
} g16_fade_t;

int  g16_fade_init(g16_fade_t *f, int w, int h);
void g16_fade_free(g16_fade_t *f);
/* Forgets the tables; the next ones have `levels` levels (2..16). */
void g16_fade_reset(g16_fade_t *f, int levels);
/* The table of colour `from`: to[0] (darkest) .. to[levels-1]. Returns -1
 * when there are already G16_FADE_COLOURS tables. */
int  g16_fade_colour(g16_fade_t *f, uint16_t from, const uint16_t *to);
/* After the tables: the scale of the colours without one. */
void g16_fade_done(g16_fade_t *f);
void g16_fade_clear(g16_fade_t *f, int ambient);
/* A lamp at screen (x, y): `level` at the centre, 0 at `radius`; dither
 * 0..256 is how much of each ring edge is mixed (0 = sharp rings, 256 = a
 * smooth ordered-dither ramp). */
void g16_fade_glow(g16_fade_t *f, int x, int y, int radius, int level, int dither);
void g16_fade_apply(g16_t *g, const g16_fade_t *f);

/* Sheet helpers */
int  g16_sheet_alloc(g16_sheet_t *s, int w, int h);
void g16_sheet_free(g16_sheet_t *s);
void g16_sheet_set(g16_sheet_t *s, int x, int y, uint16_t c, int opaque);
void g16_sheet_update_cell(g16_sheet_t *s, int cx, int cy);

#endif
