/*
 * Sprite recipes of the assistant (M30): the base of a sprite (8x8, 16x16 or
 * 32x32) from a recipe ("ship", "slime", "coin", "grass"...) and a seed.
 * Classic procedural pixel art: shapes, mirror symmetry, a ramp of shades
 * for each colour, light from the top left, an outline. The colours and the
 * size can come from the words of a request ("astronave rossa 32x32").
 * The same seed always gives the same sprite: the next seed is a variant.
 */
#ifndef AI_SPRITE_H
#define AI_SPRITE_H

#include <stdint.h>

#define SPR_MAX         32
#define SPR_CLEAR       0xFFFFFFFFu     /* a transparent pixel */
#define SPR_NO_COLOR    0xFFFFFFFFu

typedef struct {
    const char *gen;            /* recipe id */
    int w, h;                   /* 8, 16 or 32 */
    uint32_t seed;
    uint32_t color[2];          /* main / second colour, 0xRRGGBB or SPR_NO_COLOR */
    int outline;                /* 1: dark outline around the shape */
    const uint32_t *palette;    /* optional: every pixel snaps to one of these */
    int npalette;
} spr_req_t;

typedef struct {
    int w, h;
    uint32_t px[SPR_MAX * SPR_MAX];     /* row by row: 0xRRGGBB or SPR_CLEAR */
} spr_img_t;

/* the defaults of a request: 16x16, seed 1, outline, no colours */
void spr_req_init(spr_req_t *r, const char *gen);

/* colours ("rosso", "blue", "dorata"...), size ("8x8", "16", "piccolo",
 * "grande") and "senza contorno" from the words of a request */
void spr_parse(const char *text, spr_req_t *r);

/* the colour a word names ("rosso", "blue", "dorata"...), or SPR_NO_COLOR */
uint32_t spr_color_word(const char *w);

/* 0, or -1 for an unknown recipe */
int spr_make(const spr_req_t *r, spr_img_t *out);

/* the recipes: id and a short English name */
int spr_recipes(void);
const char *spr_recipe_id(int i);
const char *spr_recipe_name(int i);
int spr_find(const char *gen);

#endif
