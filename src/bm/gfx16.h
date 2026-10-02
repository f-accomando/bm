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

/* Sheet helpers */
int  g16_sheet_alloc(g16_sheet_t *s, int w, int h);
void g16_sheet_free(g16_sheet_t *s);
void g16_sheet_set(g16_sheet_t *s, int x, int y, uint16_t c, int opaque);
void g16_sheet_update_cell(g16_sheet_t *s, int cx, int cy);

#endif
