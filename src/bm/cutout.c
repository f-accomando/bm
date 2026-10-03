#include "cutout.h"
#include "decimate.h"
#include "jpeg.h"
#include "png.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURED 0x80000000u
#define GRID 96                 /* cells along the picture's longer side */
#define MAX_LOOP 4096

typedef struct { float x, y; } pt_t;

static int fail(char *err, size_t errlen, const char *what)
{
    if (err && errlen) {
        strncpy(err, what, errlen - 1);
        err[errlen - 1] = 0;
    }
    return -1;
}

/* ------------------------------------------------------------- the mask */

/* solid[y * w + x]: the subject. Transparent pixels are the background;
 * without any, the colour of the corners is. */
static uint8_t *mask_of(const uint8_t *rgba, int w, int h)
{
    uint8_t *m = malloc((size_t)w * h);
    if (!m)
        return NULL;
    int transparent = 0;
    for (size_t i = 0; i < (size_t)w * h && !transparent; i++)
        transparent = rgba[i * 4 + 3] < 128;
    if (transparent) {
        for (size_t i = 0; i < (size_t)w * h; i++)
            m[i] = rgba[i * 4 + 3] >= 128;
        return m;
    }
    /* the background colour: the mean of the four corners (they agree, or near enough) */
    int bg[3] = { 0, 0, 0 };
    const int cx[4] = { 0, w - 1, 0, w - 1 }, cy[4] = { 0, 0, h - 1, h - 1 };
    for (int k = 0; k < 4; k++)
        for (int c = 0; c < 3; c++)
            bg[c] += rgba[((size_t)cy[k] * w + cx[k]) * 4 + c];
    for (int c = 0; c < 3; c++)
        bg[c] /= 4;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        int d = 0;
        for (int c = 0; c < 3; c++)
            d += abs(rgba[i * 4 + c] - bg[c]);
        m[i] = d > 90;
    }
    return m;
}

/* the mask on a coarse grid (a cell is solid when half of it is), the
 * small specks taken away: gw x gh cells of `cell` pixels from (x0, y0) */
static uint8_t *coarse(const uint8_t *m, int w, int h, int *x0, int *y0, int *cell, int *gw, int *gh)
{
    int lo_x = w, lo_y = h, hi_x = -1, hi_y = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (m[(size_t)y * w + x]) {
                if (x < lo_x) lo_x = x;
                if (x > hi_x) hi_x = x;
                if (y < lo_y) lo_y = y;
                if (y > hi_y) hi_y = y;
            }
    if (hi_x < 0)
        return NULL;
    int bw = hi_x - lo_x + 1, bh = hi_y - lo_y + 1, big = bw > bh ? bw : bh;
    *cell = (big + GRID - 1) / GRID;
    if (*cell < 1)
        *cell = 1;
    *gw = (bw + *cell - 1) / *cell + 2;         /* a ring of empty cells around */
    *gh = (bh + *cell - 1) / *cell + 2;
    *x0 = lo_x - *cell;
    *y0 = lo_y - *cell;
    uint8_t *g = calloc((size_t)*gw * *gh, 1);
    if (!g)
        return NULL;
    for (int gy = 1; gy < *gh - 1; gy++)
        for (int gx = 1; gx < *gw - 1; gx++) {
            int n = 0, solid = 0;
            for (int y = *y0 + gy * *cell; y < *y0 + (gy + 1) * *cell; y++)
                for (int x = *x0 + gx * *cell; x < *x0 + (gx + 1) * *cell; x++) {
                    if (x < 0 || y < 0 || x >= w || y >= h)
                        continue;
                    n++;
                    solid += m[(size_t)y * w + x];
                }
            g[gy * *gw + gx] = n && solid * 2 >= n;
        }
    /* the specks: components under 1/50 of the biggest go */
    int *label = calloc((size_t)*gw * *gh, sizeof *label);
    int *area = calloc((size_t)*gw * *gh + 1, sizeof *area);
    int *stack = malloc((size_t)*gw * *gh * sizeof *stack);
    if (label && area && stack) {
        int nl = 0, biggest = 0;
        for (int i = 0; i < *gw * *gh; i++) {
            if (!g[i] || label[i])
                continue;
            int sp = 0, lab = ++nl;
            stack[sp++] = i;
            label[i] = lab;
            while (sp) {
                int j = stack[--sp];
                area[lab]++;
                int jx = j % *gw, jy = j / *gw;
                const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; d++) {
                    int nx = jx + dx[d], ny = jy + dy[d];
                    if (nx < 0 || ny < 0 || nx >= *gw || ny >= *gh)
                        continue;
                    int k = ny * *gw + nx;
                    if (g[k] && !label[k]) {
                        label[k] = lab;
                        stack[sp++] = k;
                    }
                }
            }
            if (area[lab] > biggest)
                biggest = area[lab];
        }
        for (int i = 0; i < *gw * *gh; i++)
            if (g[i] && area[label[i]] * 50 < biggest)
                g[i] = 0;
    }
    free(label);
    free(area);
    free(stack);
    return g;
}

/* ---------------------------------------------------------- the outline */

/* the outer loops of the solid cells, as polygons on the cell corners
 * (the solid on the left, walking): loops[] of pt, n[] points each */
typedef struct {
    pt_t *p;
    int n;
} loop_t;

static int trace_loops(const uint8_t *g, int gw, int gh, loop_t *loops, int max_loops)
{
    /* a directed edge from each solid/empty cell boundary: solid on the left.
     * On the corner grid (gw+1) x (gh+1): the edge leaving corner c goes
     * right / down / left / up: next[c * 4 + dir] = 1 */
    int cw = gw + 1;
    uint8_t *edge = calloc((size_t)cw * (gh + 1) * 4, 1);
    if (!edge)
        return -1;
    #define SOLID(x, y) ((x) >= 0 && (y) >= 0 && (x) < gw && (y) < gh && g[(y) * gw + (x)])
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            if (!g[y * gw + x])
                continue;
            /* y down on the picture: "left of the walk" in picture coordinates */
            if (!SOLID(x, y - 1)) edge[(y * cw + x) * 4 + 0] = 1;             /* top edge: walk right */
            if (!SOLID(x + 1, y)) edge[(y * cw + x + 1) * 4 + 1] = 1;         /* right edge: walk down */
            if (!SOLID(x, y + 1)) edge[((y + 1) * cw + x + 1) * 4 + 2] = 1;   /* bottom edge: walk left */
            if (!SOLID(x - 1, y)) edge[((y + 1) * cw + x) * 4 + 3] = 1;       /* left edge: walk up */
        }
    #undef SOLID
    int nl = 0;
    const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    for (int c = 0; c < cw * (gh + 1) && nl < max_loops; c++)
        for (int d0 = 0; d0 < 4; d0++) {
            if (!edge[c * 4 + d0])
                continue;
            pt_t *p = malloc(MAX_LOOP * sizeof *p);
            if (!p) {
                free(edge);
                return -1;
            }
            int n = 0, cur = c, dir = d0;
            while (n < MAX_LOOP) {
                p[n].x = (float)(cur % cw);
                p[n].y = (float)(cur / cw);
                n++;
                edge[cur * 4 + dir] = 0;
                int nx = cur % cw + dx[dir], ny = cur / cw + dy[dir];
                cur = ny * cw + nx;
                if (cur == c)
                    break;
                /* turn left first (the solid hugs the left), else straight, else right */
                int tried = 0;
                for (int t = 0; t < 4; t++) {
                    int nd = (dir + 3 + t) % 4;
                    if (edge[cur * 4 + nd]) {
                        dir = nd;
                        tried = 1;
                        break;
                    }
                }
                if (!tried)
                    break;
            }
            /* the signed area: an outer loop (solid on the left, y down) is clockwise on the screen */
            float a = 0;
            for (int i = 0; i < n; i++) {
                int j = (i + 1) % n;
                a += p[i].x * p[j].y - p[j].x * p[i].y;
            }
            if (a > 0 && n >= 4) {
                loops[nl].p = p;
                loops[nl].n = n;
                nl++;
            } else
                free(p);                        /* a hole: filled */
        }
    free(edge);
    return nl;
}

/* Douglas-Peucker on a closed loop */
static void dp(const pt_t *p, int a, int b, float tol2, uint8_t *keep)
{
    if (b - a < 2)
        return;
    float ax = p[a].x, ay = p[a].y, bx = p[b].x, by = p[b].y, dx = bx - ax, dy = by - ay;
    float len2 = dx * dx + dy * dy, best = -1;
    int bi = -1;
    for (int i = a + 1; i < b; i++) {
        float d2;
        if (len2 < 1e-12f)
            d2 = (p[i].x - ax) * (p[i].x - ax) + (p[i].y - ay) * (p[i].y - ay);
        else {
            float cr = (p[i].x - ax) * dy - (p[i].y - ay) * dx;
            d2 = cr * cr / len2;
        }
        if (d2 > best) {
            best = d2;
            bi = i;
        }
    }
    if (best > tol2) {
        keep[bi] = 1;
        dp(p, a, bi, tol2, keep);
        dp(p, bi, b, tol2, keep);
    }
}

static int simplify(loop_t *l, float tol)
{
    int n = l->n;
    if (n < 4)
        return n;
    /* split at the point farthest from point 0 */
    int far = 0;
    float best = -1;
    for (int i = 1; i < n; i++) {
        float d = (l->p[i].x - l->p[0].x) * (l->p[i].x - l->p[0].x) + (l->p[i].y - l->p[0].y) * (l->p[i].y - l->p[0].y);
        if (d > best) {
            best = d;
            far = i;
        }
    }
    uint8_t *keep = calloc((size_t)n + 1, 1);
    if (!keep)
        return n;
    keep[0] = keep[far] = 1;
    pt_t *ring = malloc((size_t)(n + 1) * sizeof *ring);
    if (!ring) {
        free(keep);
        return n;
    }
    memcpy(ring, l->p, (size_t)n * sizeof *ring);
    ring[n] = l->p[0];
    dp(ring, 0, far, tol * tol, keep);
    dp(ring, far, n, tol * tol, keep);
    int m = 0;
    for (int i = 0; i < n; i++)
        if (keep[i])
            l->p[m++] = l->p[i];
    free(ring);
    free(keep);
    l->n = m;
    return m;
}

/* -------------------------------------------------------- triangulation */

static float cross2(pt_t a, pt_t b, pt_t c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static int in_tri(pt_t a, pt_t b, pt_t c, pt_t p)
{
    float c1 = cross2(a, b, p), c2 = cross2(b, c, p), c3 = cross2(c, a, p);
    return (c1 >= 0 && c2 >= 0 && c3 >= 0) || (c1 <= 0 && c2 <= 0 && c3 <= 0);
}

/* ear clipping: tris gets 3 indices per triangle; the count. The loop
 * may be either way round. */
static int triangulate(const pt_t *p, int n, int *tris)
{
    if (n < 3)
        return 0;
    float area = 0;
    for (int i = 0; i < n; i++)
        area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
    int *idx = malloc((size_t)n * sizeof *idx);
    if (!idx)
        return 0;
    for (int i = 0; i < n; i++)
        idx[i] = area > 0 ? i : n - 1 - i;      /* counter-clockwise in these coordinates */
    int m = n, nt = 0, guard = 0;
    while (m > 3 && guard < 4 * n) {
        int found = 0;
        for (int i = 0; i < m && !found; i++) {
            int i0 = idx[(i + m - 1) % m], i1 = idx[i], i2 = idx[(i + 1) % m];
            if (cross2(p[i0], p[i1], p[i2]) <= 1e-9f)
                continue;                       /* a reflex corner, or flat */
            int inside = 0;
            for (int k = 0; k < m && !inside; k++) {
                int j = idx[k];
                if (j == i0 || j == i1 || j == i2)
                    continue;
                inside = in_tri(p[i0], p[i1], p[i2], p[j]);
            }
            if (inside)
                continue;
            tris[nt * 3] = i0;
            tris[nt * 3 + 1] = i1;
            tris[nt * 3 + 2] = i2;
            nt++;
            for (int k = i; k + 1 < m; k++)
                idx[k] = idx[k + 1];
            m--;
            found = 1;
        }
        if (!found) {
            /* a degenerate spot: drop the flattest corner and go on */
            int worst = 0;
            float wv = 1e30f;
            for (int i = 0; i < m; i++) {
                float c = fabsf(cross2(p[idx[(i + m - 1) % m]], p[idx[i]], p[idx[(i + 1) % m]]));
                if (c < wv) {
                    wv = c;
                    worst = i;
                }
            }
            for (int k = worst; k + 1 < m; k++)
                idx[k] = idx[k + 1];
            m--;
        }
        guard++;
    }
    if (m == 3) {
        tris[nt * 3] = idx[0];
        tris[nt * 3 + 1] = idx[1];
        tris[nt * 3 + 2] = idx[2];
        nt++;
    }
    free(idx);
    return nt;
}

/* ----------------------------------------------------------- the solid */

typedef struct {
    dec_mesh_t m;
    int cv, cf;
    int sheet;
    float sx, sy, ox, oy;       /* picture pixel -> sheet pixel */
} build_t;

static int reserve(build_t *b, int nv, int nf)
{
    if (b->m.nv + nv > b->cv) {
        int cap = b->cv ? b->cv : 256;
        while (cap < b->m.nv + nv)
            cap *= 2;
        float *v = realloc(b->m.v, (size_t)cap * 3 * sizeof *v);
        if (!v)
            return -1;
        b->m.v = v;
        b->cv = cap;
    }
    if (b->m.nf + nf > b->cf) {
        int cap = b->cf ? b->cf : 256;
        while (cap < b->m.nf + nf)
            cap *= 2;
        uint16_t *f = realloc(b->m.f, (size_t)cap * 3 * sizeof *f);
        uint32_t *c = realloc(b->m.colour, (size_t)cap * sizeof *c);
        uint16_t *uv = realloc(b->m.uv, (size_t)cap * 6 * sizeof *uv);
        if (f) b->m.f = f;
        if (c) b->m.colour = c;
        if (uv) b->m.uv = uv;
        if (!f || !c || !uv)
            return -1;
        b->cf = cap;
    }
    return 0;
}

static int vertex(build_t *b, float x, float y, float z)
{
    if (reserve(b, 1, 0) < 0)
        return -1;
    float *v = b->m.v + b->m.nv * 3;
    v[0] = x;
    v[1] = y;
    v[2] = z;
    return b->m.nv++;
}

/* the sheet pixel (x 8) of a picture point */
static uint16_t suv(const build_t *b, float px, float py, int axis)
{
    float s = axis == 0 ? b->ox + px * b->sx : b->oy + py * b->sy;
    float lim = (float)b->sheet;
    s = s < 0 ? 0 : s > lim ? lim : s;
    return (uint16_t)(s * 8 + 0.5f);
}

/* a triangle whose right-hand normal must point away from `out` (the
 * console shows the clockwise side): the corners are swapped if not */
static int face(build_t *b, int a, int c, int d, const float *out_dir, const pt_t *uva, const pt_t *uvc, const pt_t *uvd)
{
    if (reserve(b, 0, 1) < 0)
        return -1;
    const float *pa = b->m.v + a * 3, *pc = b->m.v + c * 3, *pd = b->m.v + d * 3;
    float e1[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] }, e2[3] = { pd[0] - pa[0], pd[1] - pa[1], pd[2] - pa[2] };
    float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    int swap = n[0] * out_dir[0] + n[1] * out_dir[1] + n[2] * out_dir[2] > 0;
    int f = b->m.nf++;
    uint16_t *fv = b->m.f + f * 3;
    const pt_t *uvs[3] = { uva, uvc, uvd };
    fv[0] = (uint16_t)a;
    fv[1] = (uint16_t)(swap ? d : c);
    fv[2] = (uint16_t)(swap ? c : d);
    if (swap) {
        const pt_t *t = uvs[1];
        uvs[1] = uvs[2];
        uvs[2] = t;
    }
    b->m.colour[f] = TEXTURED;
    for (int k = 0; k < 3; k++) {
        b->m.uv[f * 6 + k * 2] = suv(b, uvs[k]->x, uvs[k]->y, 0);
        b->m.uv[f * 6 + k * 2 + 1] = suv(b, uvs[k]->x, uvs[k]->y, 1);
    }
    return 0;
}

/* the picture's x, y (pixels) -> the model's x, y (blocks) */
typedef struct {
    float scale, cx, bottom, mid;
} frame_t;

static int extrude(build_t *b, const loop_t *loops, int nl, const frame_t *fr, float depth)
{
    float z0 = -depth / 2, z1 = depth / 2;
    const float front[3] = { 0, 0, -1 }, back[3] = { 0, 0, 1 };
    for (int l = 0; l < nl; l++) {
        const loop_t *lp = &loops[l];
        int n = lp->n;
        int *tris = malloc((size_t)(n - 2) * 3 * sizeof *tris);
        if (!tris)
            return -1;
        int nt = triangulate(lp->p, n, tris);
        int base = b->m.nv;
        for (int i = 0; i < n; i++) {
            float x = (lp->p[i].x - fr->cx) * fr->scale, y = (fr->bottom - lp->p[i].y) * fr->scale;
            if (vertex(b, x, y, z0) < 0 || vertex(b, x, y, z1) < 0) {
                free(tris);
                return -1;
            }
        }
        for (int t = 0; t < nt; t++) {
            int i0 = tris[t * 3], i1 = tris[t * 3 + 1], i2 = tris[t * 3 + 2];
            if (face(b, base + i0 * 2, base + i1 * 2, base + i2 * 2, front, &lp->p[i0], &lp->p[i1], &lp->p[i2]) < 0 ||
                face(b, base + i0 * 2 + 1, base + i1 * 2 + 1, base + i2 * 2 + 1, back, &lp->p[i0], &lp->p[i1], &lp->p[i2]) < 0) {
                free(tris);
                return -1;
            }
        }
        /* the sides: outward is away from the loop's middle */
        float mx = 0, my = 0;
        for (int i = 0; i < n; i++) {
            mx += lp->p[i].x / n;
            my += lp->p[i].y / n;
        }
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ex = lp->p[j].x - lp->p[i].x, ey = lp->p[j].y - lp->p[i].y;
            /* the outward side of the edge, in model coordinates (y flipped) */
            float out[3] = { ey, ex, 0 };
            float tox = (lp->p[i].x + lp->p[j].x) / 2 - mx, toy = -((lp->p[i].y + lp->p[j].y) / 2 - my);
            if (out[0] * tox + out[1] * toy < 0) {
                out[0] = -out[0];
                out[1] = -out[1];
            }
            int a0 = base + i * 2, a1 = a0 + 1, c0 = base + j * 2, c1 = c0 + 1;
            if (face(b, a0, c0, c1, out, &lp->p[i], &lp->p[j], &lp->p[j]) < 0 ||
                face(b, a0, c1, a1, out, &lp->p[i], &lp->p[j], &lp->p[i]) < 0) {
                free(tris);
                return -1;
            }
        }
        free(tris);
    }
    return 0;
}

/* the half-outline turned: a ring per row of the coarse mask */
static int lathe(build_t *b, const uint8_t *g, int gw, int gh, const frame_t *fr, int segments, float tol)
{
    /* the radius of each row: half its width; rows without solid cells skipped */
    pt_t *prof = malloc((size_t)(gh + 2) * sizeof *prof);
    if (!prof)
        return -1;
    int np = 0;
    float axis = 0;
    int rows = 0;
    for (int y = 0; y < gh; y++) {
        int lo = -1, hi = -1;
        for (int x = 0; x < gw; x++)
            if (g[y * gw + x]) {
                if (lo < 0) lo = x;
                hi = x;
            }
        if (lo < 0)
            continue;
        axis += (lo + hi + 1) / 2.0f;
        rows++;
        prof[np].x = (hi + 1 - lo) / 2.0f;      /* the radius, in cells */
        prof[np].y = y + 0.5f;
        np++;
    }
    if (np < 2) {
        free(prof);
        return -1;
    }
    axis /= rows;
    /* closed at the top and the bottom; simplified */
    loop_t l = { prof, np };
    pt_t *closed = malloc((size_t)(np + 2) * sizeof *closed);
    if (!closed) {
        free(prof);
        return -1;
    }
    closed[0].x = 0;
    closed[0].y = prof[0].y - 0.5f;
    memcpy(closed + 1, prof, (size_t)np * sizeof *prof);
    closed[np + 1].x = 0;
    closed[np + 1].y = prof[np - 1].y + 0.5f;
    l.p = closed;
    l.n = np + 2;
    simplify(&l, tol);
    np = l.n;
    /* the rings */
    const pt_t *pr = l.p;
    int *ring = malloc((size_t)np * segments * sizeof *ring);
    if (!ring) {
        free(closed);
        free(prof);
        return -1;
    }
    for (int i = 0; i < np; i++) {
        float y = (fr->bottom - pr[i].y) * fr->scale, r = pr[i].x * fr->scale;
        int apex = pr[i].x <= 1e-6f ? vertex(b, 0, y, 0) : -1;     /* the top and the bottom: one point */
        if (pr[i].x <= 1e-6f && apex < 0)
            goto bad;
        for (int s = 0; s < segments; s++) {
            float a = 2 * 3.14159265f * s / segments;
            int v = apex >= 0 ? apex : vertex(b, r * sinf(a), y, -r * cosf(a));   /* s = 0 in front (-z) */
            if (v < 0)
                goto bad;
            ring[i * segments + s] = v;
        }
    }
    for (int i = 0; i + 1 < np; i++)
        for (int s = 0; s < segments; s++) {
            int s2 = (s + 1) % segments;
            int a = ring[i * segments + s], c = ring[i * segments + s2];
            int d = ring[(i + 1) * segments + s], e = ring[(i + 1) * segments + s2];
            /* outward: away from the axis at the middle of the quad; the
             * picture projected from the front: x along sin(a) */
            float am = 2 * 3.14159265f * (s + 0.5f) / segments;
            float out[3] = { sinf(am), 0, -cosf(am) };
            float ra = pr[i].x, rd = pr[i + 1].x;
            pt_t ua = { axis + ra * sinf(2 * 3.14159265f * s / segments), pr[i].y };
            pt_t uc = { axis + ra * sinf(2 * 3.14159265f * s2 / segments), pr[i].y };
            pt_t ud = { axis + rd * sinf(2 * 3.14159265f * s / segments), pr[i + 1].y };
            pt_t ue = { axis + rd * sinf(2 * 3.14159265f * s2 / segments), pr[i + 1].y };
            if (pr[i].x > 1e-6f && pr[i + 1].x > 1e-6f) {
                if (face(b, a, c, e, out, &ua, &uc, &ue) < 0 || face(b, a, e, d, out, &ua, &ue, &ud) < 0)
                    goto bad;
            } else if (pr[i].x > 1e-6f) {
                if (face(b, a, c, d, out, &ua, &uc, &ud) < 0)
                    goto bad;
            } else if (pr[i + 1].x > 1e-6f) {
                if (face(b, a, e, d, out, &ua, &ue, &ud) < 0)
                    goto bad;
            }
        }
    free(ring);
    free(closed);
    free(prof);
    return 0;
bad:
    free(ring);
    free(closed);
    free(prof);
    return -1;
}

/* ---------------------------------------------------------------- main */

int cutout_model(const uint8_t *rgba, int w, int h, const char *name, const cutout_opts_t *o, glb_model_t *out,
                 char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    if (err && errlen)
        err[0] = 0;
    if (w < 2 || h < 2)
        return fail(err, errlen, "the picture is too small");
    uint8_t *m = mask_of(rgba, w, h);
    if (!m)
        return fail(err, errlen, "no memory for the picture");
    int x0, y0, cell, gw, gh;
    uint8_t *g = coarse(m, w, h, &x0, &y0, &cell, &gw, &gh);
    free(m);
    if (!g)
        return fail(err, errlen, "nothing in the picture (all background)");
    loop_t loops[16];
    int nl = trace_loops(g, gw, gh, loops, 16);
    if (nl <= 0) {
        free(g);
        return fail(err, errlen, nl < 0 ? "no memory for the outline" : "no outline in the picture");
    }
    /* the frame: the mask's box, feet at y = 0, `height` tall; cells -> blocks */
    int lo_x = gw, lo_y = gh, hi_x = 0, hi_y = 0;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            if (g[y * gw + x]) {
                if (x < lo_x) lo_x = x;
                if (x + 1 > hi_x) hi_x = x + 1;
                if (y < lo_y) lo_y = y;
                if (y + 1 > hi_y) hi_y = y + 1;
            }
    float height = o->height > 0 ? o->height : 2;
    frame_t fr;
    fr.scale = height / (float)(hi_y - lo_y);
    fr.cx = (lo_x + hi_x) / 2.0f;
    fr.bottom = (float)hi_y;
    fr.mid = (lo_y + hi_y) / 2.0f;
    float tol = (o->tolerance > 0 ? o->tolerance : 0.02f) * (float)(hi_y - lo_y);
    if (tol < 1.0f)
        tol = 1.0f;                             /* the stair steps of the cells are noise */
    for (int l = 0; l < nl; l++)
        simplify(&loops[l], tol);
    build_t b;
    memset(&b, 0, sizeof b);
    b.sheet = o->sheet > 0 ? o->sheet : 256;
    /* the picture's box onto the sheet, whole, the longer side filling it */
    int bw = (hi_x - lo_x) * cell, bh = (hi_y - lo_y) * cell;
    float fit = (float)b.sheet / (float)(bw > bh ? bw : bh);
    b.sx = b.sy = fit * (float)cell;            /* cells -> sheet pixels */
    b.ox = -lo_x * b.sx;
    b.oy = -lo_y * b.sy;
    int r;
    if (o->lathe)
        r = lathe(&b, g, gw, gh, &fr, o->segments > 2 ? o->segments : 12, tol);
    else
        r = extrude(&b, loops, nl, &fr, (o->depth > 0 ? o->depth : 0.2f) * height);
    for (int l = 0; l < nl; l++)
        free(loops[l].p);
    int ret = -1;
    uint8_t *tex = NULL;
    if (r < 0) {
        fail(err, errlen, "no memory for the model");
        goto out;
    }
    if (b.m.nf < 1) {
        fail(err, errlen, "no faces came out of the outline");
        goto out;
    }
    /* the sheet: the picture's box, scaled with the same fit */
    tex = calloc((size_t)b.sheet * b.sheet * 4, 1);
    if (!tex) {
        fail(err, errlen, "no memory for the sheet");
        goto out;
    }
    for (int y = 0; y < b.sheet; y++)
        for (int x = 0; x < b.sheet; x++) {
            int px = x0 + lo_x * cell + (int)(x / fit), py = y0 + lo_y * cell + (int)(y / fit);
            if (px < 0 || py < 0 || px >= w || py >= h || (int)(x / fit) >= bw || (int)(y / fit) >= bh)
                continue;
            memcpy(tex + ((size_t)y * b.sheet + x) * 4, rgba + ((size_t)py * w + px) * 4, 4);
            if (tex[((size_t)y * b.sheet + x) * 4 + 3] < 128)
                memset(tex + ((size_t)y * b.sheet + x) * 4, 0, 4);
        }
    /* the background pixels next to the figure take the colour beside
     * them (the outline runs on cell corners, a little outside the opaque
     * pixels: the edges and the sides would show holes otherwise) */
    int grow = (int)(fit * cell) + 2;
    if (grow > 12)
        grow = 12;
    uint8_t *tex2 = malloc((size_t)b.sheet * b.sheet * 4);
    for (int pass = 0; tex2 && pass < grow; pass++) {
        memcpy(tex2, tex, (size_t)b.sheet * b.sheet * 4);
        for (int y = 0; y < b.sheet; y++)
            for (int x = 0; x < b.sheet; x++) {
                size_t i = ((size_t)y * b.sheet + x) * 4;
                if (tex[i + 3] >= 128)
                    continue;
                const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; d++) {
                    int nx = x + dx[d], ny = y + dy[d];
                    if (nx < 0 || ny < 0 || nx >= b.sheet || ny >= b.sheet)
                        continue;
                    size_t j = ((size_t)ny * b.sheet + nx) * 4;
                    if (tex[j + 3] >= 128) {
                        memcpy(tex2 + i, tex + j, 3);
                        tex2[i + 3] = 255;
                        break;
                    }
                }
            }
        memcpy(tex, tex2, (size_t)b.sheet * b.sheet * 4);
    }
    free(tex2);
    if (o->max_faces > 0 && b.m.nf > o->max_faces && dec_reduce(&b.m, o->max_faces, 0) < 0) {
        fail(err, errlen, "no memory for the reducer");
        goto out;
    }
    if (b.m.nv > 4096) {
        fail(err, errlen, "too many vertices: a plainer outline (the tolerance)");
        goto out;
    }
    if (glb_pack(name, &b.m, tex, b.sheet, b.sheet, b.sheet, out) < 0) {
        fail(err, errlen, "no memory for the model");
        goto out;
    }
    ret = 0;
out:
    free(tex);
    free(g);
    free(b.m.v);
    free(b.m.f);
    free(b.m.colour);
    free(b.m.uv);
    return ret;
}

int cutout_from_file(const uint8_t *data, size_t len, const char *name, const cutout_opts_t *opts, glb_model_t *out,
                     char *err, size_t errlen)
{
    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    if (jpeg_is(data, len)) {
        uint8_t *rgb;
        char jerr[64];
        if (jpeg_decode(data, len, &rgb, &w, &h, jerr, sizeof jerr) < 0)
            return fail(err, errlen, jerr);
        rgba = malloc((size_t)w * h * 4);
        if (!rgba) {
            free(rgb);
            return fail(err, errlen, "no memory for the picture");
        }
        for (size_t i = 0; i < (size_t)w * h; i++) {
            memcpy(rgba + i * 4, rgb + i * 3, 3);
            rgba[i * 4 + 3] = 255;
        }
        free(rgb);
    } else if (png_rgba(data, len, &rgba, &w, &h) < 0)
        return fail(err, errlen, "not a PNG or JPEG picture (8 bits, no interlace)");
    int r = cutout_model(rgba, w, h, name, opts, out, err, errlen);
    free(rgba);
    return r;
}

/* ---------------------------------------------------- for the PC tools */

/* The same through plain pointers (ctypes, scripts/bmcutout.py): the
 * records and the sheet are malloc'd, cutout_release() frees them. */
int cutout_pack(const uint8_t *data, size_t len, const char *name, int lathe, float height, float depth, int segments,
                int max_faces, uint8_t **record, size_t *reclen, uint8_t **flat, size_t *flatlen, uint8_t **texture,
                char *err, size_t errlen)
{
    cutout_opts_t o = { lathe, height, depth, segments, max_faces, 256, 0.02f };
    glb_model_t m;
    if (cutout_from_file(data, len, name, &o, &m, err, errlen) < 0)
        return -1;
    *record = m.record;
    *reclen = m.record_len;
    *flat = m.flat;
    *flatlen = m.flat_len;
    *texture = m.texture;
    return 0;
}

void cutout_release(void *p)
{
    free(p);
}
