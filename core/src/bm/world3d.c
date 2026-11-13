#include "world3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void w3_init(w3_world_t *w)
{
    memset(w, 0, sizeof *w);
}

void w3_free(w3_world_t *w)
{
    free(w->box);
    memset(w, 0, sizeof *w);
}

int w3_add_box(w3_world_t *w, const float lo[3], const float hi[3], int tag)
{
    if (w->n == w->cap) {
        int cap = w->cap ? w->cap * 2 : 64;
        w3_box_t *b = realloc(w->box, (size_t)cap * sizeof *b);
        if (!b)
            return -1;
        w->box = b;
        w->cap = cap;
    }
    w3_box_t *b = &w->box[w->n];
    for (int k = 0; k < 3; k++) {
        b->lo[k] = lo[k] < hi[k] ? lo[k] : hi[k];
        b->hi[k] = lo[k] < hi[k] ? hi[k] : lo[k];
    }
    b->tag = tag;
    return w->n++;
}

int w3_ray(const w3_world_t *w, const float o[3], const float d[3], float maxt, int ground, float *t, float n[3])
{
    float best = maxt;
    int hit = -1, axis = 0, side = 0;
    if (ground && d[1] < -1e-9f) {
        float tg = -o[1] / d[1];
        if (tg >= 0 && tg < best) {
            best = tg;
            hit = -2;
            axis = 1;
            side = 1;
        }
    }
    float inv[3];
    for (int k = 0; k < 3; k++)
        inv[k] = fabsf(d[k]) > 1e-12f ? 1.0f / d[k] : (d[k] < 0 ? -1e30f : 1e30f);
    for (int i = 0; i < w->n; i++) {
        const w3_box_t *b = &w->box[i];
        float t0 = 0, t1 = best;
        int ax = -1, sd = 0, ok = 1;
        for (int k = 0; k < 3; k++) {
            float a = (b->lo[k] - o[k]) * inv[k], c = (b->hi[k] - o[k]) * inv[k];
            int s = -1;                     /* entering the low face: normal -axis */
            if (a > c) {
                float tmp = a; a = c; c = tmp;
                s = 1;
            }
            if (a > t0) {
                t0 = a;
                ax = k;
                sd = s;
            }
            if (c < t1)
                t1 = c;
            if (t0 > t1) {
                ok = 0;
                break;
            }
        }
        if (ok && ax >= 0 && t0 < best) {
            best = t0;
            hit = i;
            axis = ax;
            side = sd;
        }
    }
    if (hit == -1)
        return -1;
    *t = best;
    n[0] = n[1] = n[2] = 0;
    n[axis] = (float)side;
    return hit;
}

static int overlaps(const w3_box_t *b, float x, float y, float z, float r, float h)
{
    return x + r > b->lo[0] && x - r < b->hi[0] && z + r > b->lo[2] && z - r < b->hi[2] &&
           y + h > b->lo[1] && y < b->hi[1];
}

static int blocked_at(const w3_world_t *w, float x, float y, float z, float r, float h)
{
    for (int i = 0; i < w->n; i++)
        if (overlaps(&w->box[i], x, y, z, r, h))
            return 1;
    return 0;
}

float w3_floor(const w3_world_t *w, float x, float z, float y, float r, float step)
{
    float top = 0;
    for (int i = 0; i < w->n; i++) {
        const w3_box_t *b = &w->box[i];
        if (x + r > b->lo[0] && x - r < b->hi[0] && z + r > b->lo[2] && z - r < b->hi[2] &&
            b->hi[1] <= y + step && b->hi[1] > top)
            top = b->hi[1];
    }
    return top;
}

int w3_move(const w3_world_t *w, float p[3], float r, float h, const float d[3], float step, int was_on_ground)
{
    int flags = 0;
    /* horizontal, one axis at a time; a long move in steps no longer than r */
    float len = fabsf(d[0]) > fabsf(d[2]) ? fabsf(d[0]) : fabsf(d[2]);
    int n = (int)(len / (r > 0.05f ? r : 0.05f)) + 1;
    if (n > 16) n = 16;
    for (int s = 0; s < n; s++) {
        for (int axis = 0; axis < 3; axis += 2) {
            float delta = d[axis] / (float)n;
            if (delta == 0)
                continue;
            float np[3] = { p[0], p[1], p[2] };
            np[axis] += delta;
            int hit = -1;
            for (int i = 0; i < w->n && hit < 0; i++)
                if (overlaps(&w->box[i], np[0], np[1], np[2], r, h))
                    hit = i;
            if (hit < 0) {
                p[axis] = np[axis];
                continue;
            }
            /* a step: a low box with room above it */
            const w3_box_t *b = &w->box[hit];
            if (was_on_ground && b->hi[1] - p[1] <= step && !blocked_at(w, np[0], b->hi[1] + 0.001f, np[2], r, h)) {
                p[axis] = np[axis];
                p[1] = b->hi[1] + 0.001f;
                continue;
            }
            flags |= 2 | (axis == 0 ? 4 : 8);
        }
    }
    /* vertical */
    float ny = p[1] + d[1];
    if (d[1] <= 0) {
        float fl = w3_floor(w, p[0], p[2], p[1], r, step);
        if (ny <= fl) {
            ny = fl;
            flags |= 1;
        }
    } else {
        for (int i = 0; i < w->n; i++) {
            const w3_box_t *b = &w->box[i];
            if (overlaps(b, p[0], ny, p[2], r, h) && p[1] + h <= b->lo[1] + 0.01f) {
                ny = b->lo[1] - h;
                flags |= 16;
                break;
            }
        }
    }
    p[1] = ny;
    return flags;
}
