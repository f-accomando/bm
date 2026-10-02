/*
 * Collision worlds for 3D cartridges (Overbit, M31): solid boxes, rays and
 * moving bodies (an upright box of radius r and height h) that slide along
 * the walls and step up small heights. Portable C, no drawing.
 */
#ifndef WORLD3D_H
#define WORLD3D_H

#include <stdint.h>

typedef struct {
    float lo[3], hi[3];
    int tag;
} w3_box_t;

typedef struct {
    w3_box_t *box;
    int n, cap;
} w3_world_t;

void w3_init(w3_world_t *w);
void w3_free(w3_world_t *w);
int  w3_add_box(w3_world_t *w, const float lo[3], const float hi[3], int tag);

/* The first box hit by the ray o + t d (d need not be unit), 0 <= t <
 * maxt: returns its index (or -1), *t and the normal of the face entered.
 * The ground y = 0 counts as a box with index -2 when `ground`. */
int  w3_ray(const w3_world_t *w, const float o[3], const float d[3], float maxt, int ground, float *t, float n[3]);

/* The highest top of a box under (x, z) (radius r) at most `step` above y,
 * or 0 (the ground). */
float w3_floor(const w3_world_t *w, float x, float z, float y, float r, float step);

/* Moves a body (feet at p, radius r, height h) by d: horizontal axes one at
 * a time, stepping up to `step`; then vertical. p is updated; flags: 1 on
 * the ground, 2 a wall stopped it (x: 4, z: 8), 16 a ceiling. */
int  w3_move(const w3_world_t *w, float p[3], float r, float h, const float d[3], float step, int was_on_ground);

#endif
