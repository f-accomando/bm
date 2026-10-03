/*
 * Fewer triangles for a model: quadric edge collapse (see decimate.h).
 *
 * Every vertex carries a quadric (Garland and Heckbert 1997): the sum, over
 * the planes of its faces (weighted by their area), of the squared distance
 * from the plane as a function of the position. Collapsing the edge (a, b)
 * onto a costs (Qa + Qb)(a): the edges go cheapest first, from a heap with
 * lazy entries (an entry whose cost is no longer the edge's is skipped: a
 * fresh one was pushed when the vertex changed). A collapse keeps the
 * position of one end (half-edge collapse), so a vertex keeps its bone and
 * the texture corners of the faces stay meaningful.
 */
#include "decimate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURED 0x80000000u
#define FEATURE_W 1.0f          /* a colour line or a texture seam */
#define BORDER_W 10.0f          /* an open border */
#define MAX_GONE 16             /* faces an edge can have (more: not collapsed) */

typedef struct {
    float cost;
    uint16_t a, b;
    uint8_t dir;                /* 0: b goes onto a; 1: a onto b */
} entry_t;

typedef struct {
    dec_mesh_t *m;
    float *q;                   /* nv x 10: the quadrics */
    uint8_t *valive, *falive;
    int *vhead, *cnext;         /* the corners (face * 3 + k) of each vertex, as lists */
    entry_t *heap;
    int hn, hcap;
    float bone_pen;
    int alive_faces;
} dec_t;

/* ------------------------------------------------------------- vectors */

static void sub3(const float *a, const float *b, float *o)
{
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static void cross3(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float norm3(float *a)
{
    float l = sqrtf(dot3(a, a));
    if (l > 1e-20f) {
        a[0] /= l;
        a[1] /= l;
        a[2] /= l;
    }
    return l;
}

/* the normal (not unit) of the triangle p0 p1 p2 */
static void tri_normal(const float *p0, const float *p1, const float *p2, float *n)
{
    float e1[3], e2[3];
    sub3(p1, p0, e1);
    sub3(p2, p0, e2);
    cross3(e1, e2, n);
}

/* ------------------------------------------------------------ quadrics */

static void quad_plane(float *q, const float *n, float d, float w)
{
    q[0] += w * n[0] * n[0];
    q[1] += w * n[0] * n[1];
    q[2] += w * n[0] * n[2];
    q[3] += w * n[0] * d;
    q[4] += w * n[1] * n[1];
    q[5] += w * n[1] * n[2];
    q[6] += w * n[1] * d;
    q[7] += w * n[2] * n[2];
    q[8] += w * n[2] * d;
    q[9] += w * d * d;
}

static float quad_err(const float *q, const float *p)
{
    float x = p[0], y = p[1], z = p[2];
    float e = q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z +
              2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9];
    return e < 0 ? 0 : e;
}

/* ------------------------------------------------------------ the heap */

static int heap_valid(const dec_t *d, const entry_t *e)
{
    return d->valive[e->a] && d->valive[e->b];
}

static void heap_push(dec_t *d, float cost, int a, int b, int dir)
{
    if (d->hn == d->hcap) {
        int n = 0;                              /* the stale entries go */
        for (int i = 0; i < d->hn; i++)
            if (heap_valid(d, &d->heap[i]))
                d->heap[n++] = d->heap[i];
        d->hn = 0;
        for (int i = 0; i < n; i++)
            heap_push(d, d->heap[i].cost, d->heap[i].a, d->heap[i].b, d->heap[i].dir);   /* heapified again */
        if (d->hn >= d->hcap - 1)
            return;                             /* full of live edges: this one is lost */
    }
    int i = d->hn++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (d->heap[p].cost <= cost)
            break;
        d->heap[i] = d->heap[p];
        i = p;
    }
    d->heap[i].cost = cost;
    d->heap[i].a = (uint16_t)a;
    d->heap[i].b = (uint16_t)b;
    d->heap[i].dir = (uint8_t)dir;
}

static entry_t heap_pop(dec_t *d)
{
    entry_t top = d->heap[0];
    entry_t last = d->heap[--d->hn];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, s = i;
        float sc = last.cost;
        if (l < d->hn && d->heap[l].cost < sc) {
            s = l;
            sc = d->heap[l].cost;
        }
        if (r < d->hn && d->heap[r].cost < sc)
            s = r;
        if (s == i)
            break;
        d->heap[i] = d->heap[s];
        i = s;
    }
    if (d->hn > 0)
        d->heap[i] = last;
    return top;
}

/* --------------------------------------------------------- the corners */

#define FACE(D, F) ((D)->m->f + (F) * 3)
#define POS(D, I) ((D)->m->v + (I) * 3)
#define UV(D, F) ((D)->m->uv + (F) * 6)

static int corner_of(const dec_t *d, int f, int v)
{
    const uint16_t *fv = FACE(d, f);
    return fv[0] == v ? 0 : fv[1] == v ? 1 : fv[2] == v ? 2 : -1;
}

static void link_corner(dec_t *d, int f, int k)
{
    int v = FACE(d, f)[k], c = f * 3 + k;
    d->cnext[c] = d->vhead[v];
    d->vhead[v] = c;
}

/* --------------------------------------------------------- the welding */

static uint32_t hash_vertex(const float *p, int bone)
{
    uint32_t b[3];
    memcpy(b, p, 12);
    uint32_t h = 2166136261u;
    for (int i = 0; i < 3; i++) {
        h ^= b[i] == 0x80000000u ? 0 : b[i];    /* -0 is 0 */
        h *= 16777619u;
    }
    return (h ^ (uint32_t)bone) * 2654435761u;
}

static int same_vertex(const dec_mesh_t *m, int i, int j)
{
    const float *p = m->v + i * 3, *r = m->v + j * 3;
    return p[0] == r[0] && p[1] == r[1] && p[2] == r[2] && (!m->bone || m->bone[i] == m->bone[j]);
}

/* merges the vertices at the same position (and bone); the faces point to
 * the first of each; faces with two corners alike go */
static int weld(dec_t *d)
{
    dec_mesh_t *m = d->m;
    int cap = 1;
    while (cap < m->nv * 2)
        cap <<= 1;
    int *table = malloc((size_t)cap * sizeof *table);
    int *remap = malloc((size_t)m->nv * sizeof *remap);
    if (!table || !remap) {
        free(table);
        free(remap);
        return -1;
    }
    for (int i = 0; i < cap; i++)
        table[i] = -1;
    for (int i = 0; i < m->nv; i++) {
        uint32_t h = hash_vertex(m->v + i * 3, m->bone ? m->bone[i] : 0) & (uint32_t)(cap - 1);
        remap[i] = i;
        for (;;) {
            if (table[h] < 0) {
                table[h] = i;
                break;
            }
            if (same_vertex(m, table[h], i)) {
                remap[i] = table[h];
                break;
            }
            h = (h + 1) & (uint32_t)(cap - 1);
        }
    }
    free(table);
    d->alive_faces = 0;
    for (int f = 0; f < m->nf; f++) {
        uint16_t *fv = FACE(d, f);
        for (int k = 0; k < 3; k++)
            fv[k] = (uint16_t)remap[fv[k]];
        float n[3];
        tri_normal(POS(d, fv[0]), POS(d, fv[1]), POS(d, fv[2]), n);
        d->falive[f] = fv[0] != fv[1] && fv[1] != fv[2] && fv[0] != fv[2] && dot3(n, n) > 1e-24f;
        if (d->falive[f])
            d->alive_faces++;
    }
    for (int i = 0; i < m->nv; i++)
        d->valive[i] = remap[i] == i;
    free(remap);
    return 0;
}

/* ------------------------------------------------------- the quadrics */

typedef struct {
    uint32_t key;               /* min << 16 | max */
    int f;
} edge_t;

static int edge_cmp(const void *a, const void *b)
{
    uint32_t x = ((const edge_t *)a)->key, y = ((const edge_t *)b)->key;
    return x < y ? -1 : x > y;
}

/* the texture corners of vertex v in face f */
static const uint16_t *uv_at(const dec_t *d, int f, int v)
{
    return UV(d, f) + corner_of(d, f, v) * 2;
}

static int same_uv(const uint16_t *a, const uint16_t *b)
{
    return a[0] == b[0] && a[1] == b[1];
}

/* a feature: the faces f and g on the edge (a, b) differ in colour, or
 * their texture does not go on across the edge */
static int feature(const dec_t *d, int f, int g, int a, int b)
{
    uint32_t cf = d->m->colour[f], cg = d->m->colour[g];
    if ((cf & TEXTURED) != (cg & TEXTURED))
        return 1;
    if (!(cf & TEXTURED))
        return cf != cg;
    return !same_uv(uv_at(d, f, a), uv_at(d, g, a)) || !same_uv(uv_at(d, f, b), uv_at(d, g, b));
}

/* the plane through the edge (a, b), across face f, into both ends */
static void edge_plane(dec_t *d, int f, int a, int b, float w)
{
    const uint16_t *fv = FACE(d, f);
    float n[3], e[3], c[3];
    tri_normal(POS(d, fv[0]), POS(d, fv[1]), POS(d, fv[2]), n);
    sub3(POS(d, b), POS(d, a), e);
    float len2 = dot3(e, e);
    cross3(e, n, c);
    if (norm3(c) < 1e-20f)
        return;
    quad_plane(d->q + a * 10, c, -dot3(c, POS(d, a)), w * len2);
    quad_plane(d->q + b * 10, c, -dot3(c, POS(d, a)), w * len2);
}

static void edge_cost(const dec_t *d, int a, int b, float *ea, float *eb);

/* the cheaper way of the edge (a, b) into the heap */
static void push_edge(dec_t *d, int a, int b)
{
    float ea, eb;
    edge_cost(d, a, b, &ea, &eb);
    heap_push(d, eb < ea ? eb : ea, a, b, eb < ea);
}

/* the quadrics of the faces, then the borders and features; the edges
 * (each once) into the heap. Also the bone penalty. */
static int build(dec_t *d)
{
    dec_mesh_t *m = d->m;
    float total_area = 0;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int f = 0; f < m->nf; f++) {
        if (!d->falive[f])
            continue;
        const uint16_t *fv = FACE(d, f);
        float n[3];
        tri_normal(POS(d, fv[0]), POS(d, fv[1]), POS(d, fv[2]), n);
        float area = norm3(n) * 0.5f;
        total_area += area;
        float dd = -dot3(n, POS(d, fv[0]));
        for (int k = 0; k < 3; k++) {
            quad_plane(d->q + fv[k] * 10, n, dd, area);
            link_corner(d, f, k);
            for (int j = 0; j < 3; j++) {
                float c = POS(d, fv[k])[j];
                if (c < lo[j])
                    lo[j] = c;
                if (c > hi[j])
                    hi[j] = c;
            }
        }
    }
    float diag2 = 0;
    for (int j = 0; j < 3; j++)
        diag2 += (hi[j] - lo[j]) * (hi[j] - lo[j]);
    d->bone_pen = d->alive_faces ? diag2 * 0.0025f * total_area / (float)d->alive_faces : 0;

    edge_t *edges = malloc((size_t)m->nf * 3 * sizeof *edges);
    if (!edges)
        return -1;
    int ne = 0;
    for (int f = 0; f < m->nf; f++) {
        if (!d->falive[f])
            continue;
        const uint16_t *fv = FACE(d, f);
        for (int k = 0; k < 3; k++) {
            uint32_t a = fv[k], b = fv[(k + 1) % 3];
            edges[ne].key = a < b ? a << 16 | b : b << 16 | a;
            edges[ne++].f = f;
        }
    }
    qsort(edges, (size_t)ne, sizeof *edges, edge_cmp);
    for (int i = 0; i < ne;) {
        int j = i + 1;
        while (j < ne && edges[j].key == edges[i].key)
            j++;
        int a = (int)(edges[i].key >> 16), b = (int)(edges[i].key & 0xFFFF);
        if (j - i == 1)
            edge_plane(d, edges[i].f, a, b, BORDER_W);
        else if (feature(d, edges[i].f, edges[i + 1].f, a, b))
            edge_plane(d, edges[i].f, a, b, FEATURE_W);
        i = j;
    }
    for (int i = 0; i < ne;) {
        int j = i + 1;
        while (j < ne && edges[j].key == edges[i].key)
            j++;
        push_edge(d, (int)(edges[i].key >> 16), (int)(edges[i].key & 0xFFFF));
        i = j;
    }
    free(edges);
    return 0;
}

/* --------------------------------------------------------- collapsing */

/* the cost of collapsing the edge (a, b) onto a (*ea) and onto b (*eb) */
static void edge_cost(const dec_t *d, int a, int b, float *ea, float *eb)
{
    float q[10];
    for (int i = 0; i < 10; i++)
        q[i] = d->q[a * 10 + i] + d->q[b * 10 + i];
    *ea = quad_err(q, POS(d, a));
    *eb = quad_err(q, POS(d, b));
    if (d->m->bone && d->m->bone[a] != d->m->bone[b]) {
        *ea += d->bone_pen;
        *eb += d->bone_pen;
    }
}

/* can `gone` go onto `surv`? No face of `gone` may turn over or flatten,
 * the two must share a face, and faces must remain. */
static int collapse_ok(const dec_t *d, int surv, int gone)
{
    int shared = 0;
    for (int c = d->vhead[gone]; c >= 0; c = d->cnext[c]) {
        int f = c / 3;
        if (!d->falive[f])
            continue;
        if (corner_of(d, f, surv) >= 0) {
            if (++shared > MAX_GONE)
                return 0;
            continue;
        }
        const uint16_t *fv = FACE(d, f);
        const float *p[3];
        float n0[3], n1[3];
        for (int k = 0; k < 3; k++)
            p[k] = POS(d, fv[k]);
        tri_normal(p[0], p[1], p[2], n0);
        p[c % 3] = POS(d, surv);
        tri_normal(p[0], p[1], p[2], n1);
        float l0 = sqrtf(dot3(n0, n0)), l1 = sqrtf(dot3(n1, n1));
        if (l1 < 1e-12f || dot3(n0, n1) < 0.05f * l0 * l1)
            return 0;
    }
    return shared > 0 && d->alive_faces - shared >= 1;
}

/* the texture corner of `surv` for face f, whose corner k was `gone`:
 * from a face that goes (the same texture at `gone`), else the texture of
 * f stretched to the new position */
static void move_uv(const dec_t *d, int f, int k, int surv, int gone, const int *gone_faces, int ngone)
{
    uint16_t *uv = UV(d, f);
    for (int i = 0; i < ngone; i++) {
        int g = gone_faces[i];
        if (!(d->m->colour[g] & TEXTURED))
            continue;
        if (same_uv(uv + k * 2, uv_at(d, g, gone))) {
            const uint16_t *s = uv_at(d, g, surv);
            uv[k * 2] = s[0];
            uv[k * 2 + 1] = s[1];
            return;
        }
    }
    const uint16_t *fv = FACE(d, f);
    int k1 = (k + 1) % 3, k2 = (k + 2) % 3;
    float e1[3], e2[3], dd[3];
    sub3(POS(d, fv[k1]), POS(d, gone), e1);
    sub3(POS(d, fv[k2]), POS(d, gone), e2);
    sub3(POS(d, surv), POS(d, gone), dd);
    float a = dot3(e1, e1), b = dot3(e1, e2), c = dot3(e2, e2), det = a * c - b * b;
    if (fabsf(det) < 1e-20f)
        return;
    float r1 = dot3(dd, e1), r2 = dot3(dd, e2);
    float l1 = (r1 * c - r2 * b) / det, l2 = (a * r2 - b * r1) / det;
    for (int j = 0; j < 2; j++) {
        float u = uv[k * 2 + j] + l1 * ((float)uv[k1 * 2 + j] - uv[k * 2 + j]) +
                  l2 * ((float)uv[k2 * 2 + j] - uv[k * 2 + j]);
        uv[k * 2 + j] = (uint16_t)(u < 0 ? 0 : u > 65535 ? 65535 : u + 0.5f);
    }
}

static void collapse(dec_t *d, int surv, int gone)
{
    int gone_faces[MAX_GONE], ngone = 0;
    for (int c = d->vhead[gone]; c >= 0; c = d->cnext[c]) {
        int f = c / 3;
        if (d->falive[f] && corner_of(d, f, surv) >= 0 && ngone < MAX_GONE)
            gone_faces[ngone++] = f;
    }
    int tail = -1;
    for (int c = d->vhead[gone]; c >= 0; c = d->cnext[c]) {
        int f = c / 3, k = c % 3;
        tail = c;
        if (!d->falive[f])
            continue;
        if (corner_of(d, f, surv) >= 0) {
            d->falive[f] = 0;
            d->alive_faces--;
            continue;
        }
        if (d->m->colour[f] & TEXTURED)
            move_uv(d, f, k, surv, gone, gone_faces, ngone);
        FACE(d, f)[k] = (uint16_t)surv;
    }
    if (tail >= 0) {
        d->cnext[tail] = d->vhead[surv];
        d->vhead[surv] = d->vhead[gone];
        d->vhead[gone] = -1;
    }
    for (int i = 0; i < 10; i++)
        d->q[surv * 10 + i] += d->q[gone * 10 + i];
    d->valive[gone] = 0;
    /* two faces of surv on the same three vertices: a fold (both go) or a
     * double (one goes) */
    for (int c = d->vhead[surv]; c >= 0; c = d->cnext[c]) {
        int f = c / 3;
        if (!d->falive[f])
            continue;
        for (int c2 = d->cnext[c]; c2 >= 0; c2 = d->cnext[c2]) {
            int g = c2 / 3;
            if (!d->falive[g] || g == f)
                continue;
            const uint16_t *fv = FACE(d, f);
            int k0 = corner_of(d, g, fv[0]), k1 = corner_of(d, g, fv[1]), k2 = corner_of(d, g, fv[2]);
            if (k0 < 0 || k1 < 0 || k2 < 0)
                continue;
            int same = (k1 - k0 + 3) % 3 == 1;      /* the same way round */
            d->falive[g] = 0;
            d->alive_faces--;
            if (!same) {
                d->falive[f] = 0;
                d->alive_faces--;
                break;
            }
        }
    }
    /* the edges of surv, with their new costs */
    for (int c = d->vhead[surv]; c >= 0; c = d->cnext[c]) {
        int f = c / 3;
        if (!d->falive[f])
            continue;
        const uint16_t *fv = FACE(d, f);
        for (int k = 0; k < 3; k++) {
            int x = fv[k];
            if (x == surv)
                continue;
            /* each neighbour once: from the face where it follows surv */
            if (fv[(k + 2) % 3] != surv)
                continue;
            push_edge(d, surv < x ? surv : x, surv < x ? x : surv);
        }
    }
}

/* ------------------------------------------------------------- compact */

static void compact(dec_t *d)
{
    dec_mesh_t *m = d->m;
    int *newi = malloc((size_t)m->nv * sizeof *newi);
    if (!newi)
        return;
    for (int i = 0; i < m->nv; i++)
        newi[i] = -1;
    int nf = 0;
    for (int f = 0; f < m->nf; f++) {
        if (!d->falive[f])
            continue;
        const uint16_t *fv = FACE(d, f);
        for (int k = 0; k < 3; k++)
            newi[fv[k]] = 0;
        if (nf != f) {
            memcpy(m->f + nf * 3, fv, 3 * sizeof *fv);
            m->colour[nf] = m->colour[f];
            memcpy(m->uv + nf * 6, UV(d, f), 6 * sizeof *m->uv);
        }
        nf++;
    }
    int nv = 0;
    for (int i = 0; i < m->nv; i++) {
        if (newi[i] < 0)
            continue;
        newi[i] = nv;
        if (nv != i) {
            memcpy(m->v + nv * 3, m->v + i * 3, 3 * sizeof *m->v);
            if (m->bone)
                m->bone[nv] = m->bone[i];
        }
        nv++;
    }
    for (int f = 0; f < nf; f++)
        for (int k = 0; k < 3; k++)
            m->f[f * 3 + k] = (uint16_t)newi[m->f[f * 3 + k]];
    m->nv = nv;
    m->nf = nf;
    free(newi);
}

int dec_reduce(dec_mesh_t *m, int target, float max_err)
{
    dec_t d;
    memset(&d, 0, sizeof d);
    d.m = m;
    if (m->nv <= 0 || m->nf <= 0)
        return m->nf;
    if (target < 1)
        target = 1;
    d.hcap = m->nf * 3 + m->nv * 16 + 256;
    d.q = calloc((size_t)m->nv * 10, sizeof *d.q);
    d.valive = malloc((size_t)m->nv);
    d.falive = malloc((size_t)m->nf);
    d.vhead = malloc((size_t)m->nv * sizeof *d.vhead);
    d.cnext = malloc((size_t)m->nf * 3 * sizeof *d.cnext);
    d.heap = malloc((size_t)d.hcap * sizeof *d.heap);
    int ret = -1;
    if (!d.q || !d.valive || !d.falive || !d.vhead || !d.cnext || !d.heap)
        goto out;
    for (int i = 0; i < m->nv; i++)
        d.vhead[i] = -1;
    if (weld(&d) < 0 || build(&d) < 0)
        goto out;
    while (d.alive_faces > target && d.hn > 0) {
        entry_t e = heap_pop(&d);
        if (!heap_valid(&d, &e))
            continue;
        float ea, eb;
        edge_cost(&d, e.a, e.b, &ea, &eb);
        float cost = e.dir ? eb : ea, other = e.dir ? ea : eb;
        if (fabsf(cost - e.cost) > 1e-6f * (1 + cost))
            continue;                           /* stale: a fresh entry is in the heap */
        if (max_err > 0 && cost > max_err)
            break;
        int surv = e.dir ? e.b : e.a, gone = e.dir ? e.a : e.b;
        if (collapse_ok(&d, surv, gone))
            collapse(&d, surv, gone);
        else if (e.dir == (eb < ea))
            heap_push(&d, other, e.a, e.b, !e.dir);   /* the other way, at its own cost (once) */
    }
    compact(&d);
    ret = m->nf;
out:
    free(d.q);
    free(d.valive);
    free(d.falive);
    free(d.vhead);
    free(d.cnext);
    free(d.heap);
    return ret;
}

/* ---------------------------------------------------------- the record */

static uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xFFFF); wr16(p + 2, v >> 16); }

static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void wrf(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    wr32(p, u);
}

int bm_model_reduce(const uint8_t *rec, size_t len, const uint8_t *vb, int target, float max_err,
                    uint8_t *out, size_t *outlen, uint8_t *vb_out)
{
    if (len < 24)
        return -1;
    int nv = (int)rd16(rec + 16), nf = (int)rd16(rec + 18);
    if (nv < 1 || nf < 1 || len != 24 + (size_t)nv * 12 + (size_t)nf * 24)
        return -1;
    dec_mesh_t m;
    m.nv = nv;
    m.nf = nf;
    m.v = malloc((size_t)nv * 3 * sizeof *m.v);
    m.bone = vb ? malloc((size_t)nv) : NULL;
    m.f = malloc((size_t)nf * 3 * sizeof *m.f);
    m.colour = malloc((size_t)nf * sizeof *m.colour);
    m.uv = malloc((size_t)nf * 6 * sizeof *m.uv);
    int ret = -2;
    if (!m.v || (vb && !m.bone) || !m.f || !m.colour || !m.uv)
        goto out;
    const uint8_t *p = rec + 24;
    for (int i = 0; i < nv * 3; i++, p += 4)
        m.v[i] = rdf(p);
    if (vb)
        memcpy(m.bone, vb, (size_t)nv);
    for (int f = 0; f < nf; f++, p += 24) {
        for (int k = 0; k < 3; k++) {
            uint32_t i = rd16(p + k * 2);
            if (i >= (uint32_t)nv) {
                ret = -1;
                goto out;
            }
            m.f[f * 3 + k] = (uint16_t)i;
        }
        m.colour[f] = rd32(p + 8);
        for (int k = 0; k < 6; k++)
            m.uv[f * 6 + k] = (uint16_t)rd16(p + 12 + k * 2);
    }
    ret = dec_reduce(&m, target, max_err);
    if (ret < 0) {
        ret = -2;
        goto out;
    }
    memcpy(out, rec, 16);
    wr16(out + 16, (uint32_t)m.nv);
    wr16(out + 18, (uint32_t)m.nf);
    wr32(out + 20, 0);
    uint8_t *o = out + 24;
    for (int i = 0; i < m.nv * 3; i++, o += 4)
        wrf(o, m.v[i]);
    for (int f = 0; f < m.nf; f++, o += 24) {
        for (int k = 0; k < 3; k++)
            wr16(o + k * 2, m.f[f * 3 + k]);
        wr16(o + 6, 0);
        wr32(o + 8, m.colour[f]);
        for (int k = 0; k < 6; k++)
            wr16(o + 12 + k * 2, m.uv[f * 6 + k]);
    }
    *outlen = (size_t)(o - out);
    if (vb && vb_out)
        memcpy(vb_out, m.bone, (size_t)m.nv);
out:
    free(m.v);
    free(m.bone);
    free(m.f);
    free(m.colour);
    free(m.uv);
    return ret;
}
