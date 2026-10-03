/*
 * A model from a picture without any neural network (M30, the console's
 * own way): the picture's outline (its transparent or plain background
 * taken away) becomes a polygon, simplified, and the polygon a solid: a
 * cutout with some thickness (a paper figure: the picture on the front,
 * mirrored on the back, the edge colours on the sides) or a lathe (the
 * half-outline turned around the vertical axis: vases, towers, rockets,
 * with the picture projected on the front). The picture goes on the
 * sheet. Plain C, kernel and PC. The result has glb.h's shape.
 */
#ifndef CUTOUT_H
#define CUTOUT_H

#include "glb.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int lathe;                  /* 0: a cutout with thickness; 1: turned around the axis */
    float height;               /* the model's height in blocks (2) */
    float depth;                /* cutout: the thickness as a fraction of the height (0.2) */
    int segments;               /* lathe: around (12) */
    int max_faces;              /* reduce to at most this many triangles (0: no) */
    int sheet;                  /* the texture's side on the sheet (256) */
    float tolerance;            /* the outline's simplification, a fraction of the height (0.02) */
} cutout_opts_t;

/* The picture as RGBA (w x h): its background is what is transparent, or
 * the colour of its corners. 0, or -1 with err. glb_model_free(out) after. */
int cutout_model(const uint8_t *rgba, int w, int h, const char *name, const cutout_opts_t *opts, glb_model_t *out,
                 char *err, size_t errlen);
/* The same from a PNG or JPEG file's bytes. */
int cutout_from_file(const uint8_t *data, size_t len, const char *name, const cutout_opts_t *opts, glb_model_t *out,
                     char *err, size_t errlen);

#endif
