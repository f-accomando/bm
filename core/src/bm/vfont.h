/*
 * TrueType vector font rasterizer with glyph caching and alpha blending.
 */
#ifndef VFONT_H
#define VFONT_H

#include <stdint.h>
#include <stddef.h>
#include "gfx16.h"

typedef struct {
    int w, h;
    int xoff, yoff;
    int advance_x;
    uint8_t *alpha;     /* w * h alpha bitmap (8-bit coverage 0..255), or NULL */
    uint8_t rendered;   /* 1 if rasterization has been performed */
} g16_vglyph_t;

typedef struct g16_vfont {
    void *info;                 /* stbtt_fontinfo struct pointer */
    const uint8_t *data;        /* pointer to TTF font data */
    size_t data_len;
    int owns_data;              /* 1 if data buffer should be free()d */
    float size;                 /* pixel height, default e.g. 16.0f */
    float scale;                /* stbtt scale for pixel height */
    int ascent, descent, line_gap; /* scaled metrics in pixels */
    int line_height;            /* ascent - descent + line_gap */
    g16_vglyph_t glyphs[96];    /* glyph cache for ASCII 32..127 */
} g16_vfont_t;

/* Initialize or reinitialize vfont from memory buffer */
int  g16_vfont_init(g16_vfont_t *vf, const uint8_t *data, size_t len, int owns_data, float size);

/* Release vfont resources (cached glyph bitmaps and owned data) */
void g16_vfont_free(g16_vfont_t *vf);

/* Change font size (clears cached glyph bitmaps and recomputes scale/metrics) */
void g16_vfont_size(g16_vfont_t *vf, float size);

/* Render vector text at (x, y) with 8-bit alpha blending; returns next x */
int  g16_vprint(g16_t *g, g16_vfont_t *vf, const char *text, int x, int y, uint16_t col);

/* Measure text dimensions (w, h) in pixels */
void g16_vmeasure(g16_vfont_t *vf, const char *text, int *out_w, int *out_h);

/* Get the built-in default TTF font data */
const uint8_t *g16_vfont_default_data(size_t *out_len);

#endif
