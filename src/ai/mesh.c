/*
 * 3D recipes (see mesh.h): the canvas and its primitives, the words of a
 * request, the simple shapes and the common objects. The characters, with
 * their skeletons and animations, are in mesh_chars.c.
 *
 * A face shows from the side where its corners go clockwise (r3d.c): the
 * primitives give every face to face_out() with a point inside the solid,
 * so the side that shows is the one away from it.
 */
#include "mesh_int.h"
#include "sprite.h"
#include "text.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- random */

float mrnd(mc_t *c)
{
    uint32_t x = c->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

int mri(mc_t *c, int a, int b) { return a + (int)(mrnd(c) * (float)(b - a + 1)); }
float mrf(mc_t *c, float a, float b) { return a + mrnd(c) * (b - a); }
uint32_t mpick(mc_t *c, const uint32_t *list, int n) { return list[mri(c, 0, n - 1)]; }

/* ---------------------------------------------------------------- colours */

const uint32_t MP_BRIGHT[8] = { 0xD83A3A, 0x3CB043, 0x3A62D8, 0xF0D040, 0xF08A30, 0x8A4AD0, 0xF080B0, 0x48B8E8 };
const uint32_t MP_SKIN[5] = { 0xF6C9A0, 0xE8B088, 0xC88A60, 0x9A6440, 0x6E4630 };
const uint32_t MP_HAIR[7] = { 0x3A2A20, 0x6A4028, 0xC88A40, 0xE8D070, 0xB03A2A, 0x303040, 0xE0E0E8 };
const uint32_t MP_METAL[4] = { 0xB8C0CC, 0x9AA4B4, 0xD0D4DC, 0x8A90A0 };
const uint32_t MP_WOOD[4] = { 0x9A6234, 0x8A5A30, 0xA87040, 0x7A4A28 };
const uint32_t MP_LEAF[4] = { 0x3CA048, 0x2E8A3C, 0x58B848, 0x3C9060 };
const uint32_t MP_STONE[4] = { 0x8A8A9A, 0x9A9488, 0x7A7C88, 0xA8A8A0 };
const uint32_t MP_CLOTH[6] = { 0xD83A3A, 0x3A62D8, 0x3CB043, 0xF08A30, 0x8A4AD0, 0xE8D070 };

static uint32_t rgb(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* as the sprites' ramp: shadows lean to blue, lights to yellow */
uint32_t mshade(uint32_t c, int level)
{
    int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
    switch (level) {
    case 0: return rgb(r * 45 / 100, g * 45 / 100, b * 52 / 100 + 14);
    case 1: return rgb(r * 70 / 100, g * 71 / 100, b * 78 / 100 + 10);
    case 3: return rgb(r + (255 - r) * 30 / 100 + 6, g + (255 - g) * 28 / 100 + 3, b + (255 - b) * 20 / 100);
    case 4: return rgb(r + (255 - r) * 62 / 100, g + (255 - g) * 60 / 100, b + (255 - b) * 52 / 100);
    default: return c;
    }
}

uint32_t mmix(uint32_t a, uint32_t b, float k)
{
    int ar = (int)(a >> 16 & 255), ag = (int)(a >> 8 & 255), ab = (int)(a & 255);
    int br = (int)(b >> 16 & 255), bg = (int)(b >> 8 & 255), bb = (int)(b & 255);
    return rgb(ar + (int)((float)(br - ar) * k), ag + (int)((float)(bg - ag) * k), ab + (int)((float)(bb - ab) * k));
}

static uint8_t locked[NMAT];        /* materials the request's words chose */

void mat(mc_t *c, int m, uint32_t color)
{
    if (m < 0 || m >= NMAT || locked[m])
        return;
    c->col[m] = color;
    c->flat[m] = 0;
}

void mat_flat(mc_t *c, int m, uint32_t color)
{
    if (m < 0 || m >= NMAT)
        return;
    if (!locked[m])
        c->col[m] = color;
    c->flat[m] = 1;
}

/* ---------------------------------------------------------------- transform */

void tf_set(mc_t *c, float x, float y, float z, float rx, float ry, float rz)
{
    float ax = rx * PI_F / 180.0f, ay = ry * PI_F / 180.0f, az = rz * PI_F / 180.0f;
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    /* R = Rz Ry Rx, as draw3d turns */
    float *t = c->tf;
    t[0] = cz * cy;  t[1] = cz * sy * sx - sz * cx;  t[2] = cz * sy * cx + sz * sx;  t[3] = x;
    t[4] = sz * cy;  t[5] = sz * sy * sx + cz * cx;  t[6] = sz * sy * cx - cz * sx;  t[7] = y;
    t[8] = -sy;      t[9] = cy * sx;                 t[10] = cy * cx;                t[11] = z;
    c->tf_on = 1;
}

void tf_off(mc_t *c) { c->tf_on = 0; }

static void apply(const mc_t *c, const float *p, float *o)
{
    if (!c->tf_on) {
        o[0] = p[0];
        o[1] = p[1];
        o[2] = p[2];
        return;
    }
    const float *t = c->tf;
    o[0] = t[0] * p[0] + t[1] * p[1] + t[2] * p[2] + t[3];
    o[1] = t[4] * p[0] + t[5] * p[1] + t[6] * p[2] + t[7];
    o[2] = t[8] * p[0] + t[9] * p[1] + t[10] * p[2] + t[11];
}

/* ---------------------------------------------------------------- faces */

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

static void normal_of(const float (*p)[3], int n, float *o)
{
    float a[3], b[3], d[3];
    sub3(p[1], p[0], a);
    sub3(p[2], p[0], b);
    cross3(a, b, o);
    if (n == 4) {
        sub3(p[3], p[0], d);
        cross3(b, d, a);
        o[0] += a[0];
        o[1] += a[1];
        o[2] += a[2];
    }
}

void face(mc_t *c, const float (*p)[3], int n, int m)
{
    mesh_model_t *M = c->m;
    if (M->nfaces >= MESH_MAX_FACES || n < 3 || n > 4)
        return;
    mesh_face_t *f = &M->faces[M->nfaces];
    for (int k = 0; k < n; k++) {
        apply(c, p[k], f->p[k]);
        f->b[k] = (uint8_t)c->bone;
    }
    float nn[3];
    normal_of((const float (*)[3])f->p, n, nn);
    float len = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    if (len < 1e-7f)
        return;                         /* nothing to see */
    float ny = nn[1] / len;
    int level = 2;
    if (m < 0 || m >= NMAT) m = 1;
    if (!c->flat[m])
        level = ny > 0.5f ? 3 : ny < -0.5f ? 1 : 2;
    f->c = mshade(c->col[m], level);
    f->n = (uint8_t)n;
    M->nfaces++;
}

void face_out(mc_t *c, const float (*p)[3], int n, int m, float ox, float oy, float oz)
{
    float nn[3], mid[3] = { 0, 0, 0 };
    normal_of(p, n, nn);
    for (int k = 0; k < n; k++) {
        mid[0] += p[k][0] / (float)n;
        mid[1] += p[k][1] / (float)n;
        mid[2] += p[k][2] / (float)n;
    }
    float d = nn[0] * (mid[0] - ox) + nn[1] * (mid[1] - oy) + nn[2] * (mid[2] - oz);
    if (d >= 0) {
        face(c, p, n, m);
        return;
    }
    float r[4][3];
    for (int k = 0; k < n; k++) {
        r[k][0] = p[n - 1 - k][0];
        r[k][1] = p[n - 1 - k][1];
        r[k][2] = p[n - 1 - k][2];
    }
    face(c, (const float (*)[3])r, n, m);
}

/* a convex ring of points as quads (and a last triangle) */
static void polygon(mc_t *c, const float (*p)[3], int n, int m, float ox, float oy, float oz)
{
    if (n <= 4) {
        face_out(c, p, n, m, ox, oy, oz);
        return;
    }
    int i = 1;
    while (i + 1 < n) {
        float q[4][3];
        int k = i + 2 < n ? 4 : 3;
        memcpy(q[0], p[0], sizeof q[0]);
        memcpy(q[1], p[i], sizeof q[0]);
        memcpy(q[2], p[i + 1], sizeof q[0]);
        if (k == 4) memcpy(q[3], p[i + 2], sizeof q[0]);
        face_out(c, (const float (*)[3])q, k, m, ox, oy, oz);
        i += 2;
    }
}

/* ---------------------------------------------------------------- solids */

void box(mc_t *c, float x0, float y0, float z0, float x1, float y1, float z1, int m)
{
    float ox = (x0 + x1) / 2, oy = (y0 + y1) / 2, oz = (z0 + z1) / 2;
    const float q[6][4][3] = {
        { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 } },
        { { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { x0, y0, z1 } },
        { { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 }, { x0, y0, z0 } },
        { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } },
        { { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 } },
        { { x0, y0, z1 }, { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 } },
    };
    for (int i = 0; i < 6; i++)
        face_out(c, q[i], 4, m, ox, oy, oz);
}

void bx(mc_t *c, float x, float y, float z, float w, float h, float d, int m)
{
    box(c, x - w / 2, y, z - d / 2, x + w / 2, y + h, z + d / 2, m);
}

static void basis(const float *d, float *u, float *v)
{
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-7f) len = 1;
    float n[3] = { d[0] / len, d[1] / len, d[2] / len };
    const float up[3] = { 0, 1, 0 }, fwd[3] = { 0, 0, 1 };
    if (fabsf(n[1]) > 0.9f)
        cross3(fwd, n, u);
    else
        cross3(up, n, u);
    float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul < 1e-7f) ul = 1;
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    cross3(n, u, v);
}

void tube(mc_t *c, const float *a, const float *b, float ra, float rb, int n, int m, int caps)
{
    float d[3], u[3], v[3];
    float A[16][3], B[16][3];
    if (n < 3) n = 3;
    if (n > 16) n = 16;
    sub3(b, a, d);
    basis(d, u, v);
    float mid[3] = { (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 };
    for (int i = 0; i < n; i++) {
        float t = 2 * PI_F * ((float)i + 0.5f) / (float)n;
        float cu = cosf(t), sv = sinf(t);
        for (int k = 0; k < 3; k++) {
            A[i][k] = a[k] + ra * (cu * u[k] + sv * v[k]);
            B[i][k] = b[k] + rb * (cu * u[k] + sv * v[k]);
        }
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        if (rb <= 0) {
            const float t[3][3] = { { A[i][0], A[i][1], A[i][2] }, { A[j][0], A[j][1], A[j][2] }, { b[0], b[1], b[2] } };
            face_out(c, t, 3, m, mid[0], mid[1], mid[2]);
        } else if (ra <= 0) {
            const float t[3][3] = { { a[0], a[1], a[2] }, { B[j][0], B[j][1], B[j][2] }, { B[i][0], B[i][1], B[i][2] } };
            face_out(c, t, 3, m, mid[0], mid[1], mid[2]);
        } else {
            const float q[4][3] = { { A[i][0], A[i][1], A[i][2] }, { A[j][0], A[j][1], A[j][2] },
                                    { B[j][0], B[j][1], B[j][2] }, { B[i][0], B[i][1], B[i][2] } };
            face_out(c, q, 4, m, mid[0], mid[1], mid[2]);
        }
    }
    if (caps) {
        if (ra > 0) polygon(c, (const float (*)[3])A, n, m, mid[0], mid[1], mid[2]);
        if (rb > 0) polygon(c, (const float (*)[3])B, n, m, mid[0], mid[1], mid[2]);
    }
}

void cyl(mc_t *c, float x, float y, float z, float r, float h, int n, int m)
{
    const float a[3] = { x, y, z }, b[3] = { x, y + h, z };
    tube(c, a, b, r, r, n, m, 1);
}

void ell(mc_t *c, float x, float y, float z, float rx, float ry, float rz, int n, int m)
{
    if (n < 4) n = 4;
    if (n > 16) n = 16;
    int nr = n / 2;
    if (nr < 2) nr = 2;
    float R[8][16][3];
    if (nr > 8) nr = 8;
    for (int j = 1; j < nr; j++) {
        float ph = PI_F * (float)j / (float)nr;
        float cy = cosf(ph), sy = sinf(ph);
        for (int i = 0; i < n; i++) {
            float t = 2 * PI_F * ((float)i + 0.5f) / (float)n;
            R[j][i][0] = x + rx * sy * cosf(t);
            R[j][i][1] = y + ry * cy;
            R[j][i][2] = z + rz * sy * sinf(t);
        }
    }
    const float top[3] = { x, y + ry, z }, bot[3] = { x, y - ry, z };
    for (int i = 0; i < n; i++) {
        int k = (i + 1) % n;
        const float t1[3][3] = { { top[0], top[1], top[2] }, { R[1][i][0], R[1][i][1], R[1][i][2] },
                                 { R[1][k][0], R[1][k][1], R[1][k][2] } };
        face_out(c, t1, 3, m, x, y, z);
        for (int j = 1; j + 1 < nr; j++) {
            const float q[4][3] = { { R[j][i][0], R[j][i][1], R[j][i][2] }, { R[j][k][0], R[j][k][1], R[j][k][2] },
                                    { R[j + 1][k][0], R[j + 1][k][1], R[j + 1][k][2] },
                                    { R[j + 1][i][0], R[j + 1][i][1], R[j + 1][i][2] } };
            face_out(c, q, 4, m, x, y, z);
        }
        const float t2[3][3] = { { bot[0], bot[1], bot[2] }, { R[nr - 1][k][0], R[nr - 1][k][1], R[nr - 1][k][2] },
                                 { R[nr - 1][i][0], R[nr - 1][i][1], R[nr - 1][i][2] } };
        face_out(c, t2, 3, m, x, y, z);
    }
}

void prism(mc_t *c, int axis, const float *uv, int n, float w0, float w1, int m)
{
    float A[16][3], B[16][3], mid[3] = { 0, 0, 0 };
    if (n < 3) return;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        float u = uv[i * 2], v = uv[i * 2 + 1];
        float *a = A[i], *b = B[i];
        if (axis == 0) { a[0] = w0; a[1] = u; a[2] = v; b[0] = w1; b[1] = u; b[2] = v; }
        else if (axis == 1) { a[0] = u; a[1] = w0; a[2] = v; b[0] = u; b[1] = w1; b[2] = v; }
        else { a[0] = u; a[1] = v; a[2] = w0; b[0] = u; b[1] = v; b[2] = w1; }
        for (int k = 0; k < 3; k++) mid[k] += (a[k] + b[k]) / (float)(2 * n);
    }
    polygon(c, (const float (*)[3])A, n, m, mid[0], mid[1], mid[2]);
    polygon(c, (const float (*)[3])B, n, m, mid[0], mid[1], mid[2]);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        const float q[4][3] = { { A[i][0], A[i][1], A[i][2] }, { A[j][0], A[j][1], A[j][2] },
                                { B[j][0], B[j][1], B[j][2] }, { B[i][0], B[i][1], B[i][2] } };
        face_out(c, q, 4, m, mid[0], mid[1], mid[2]);
    }
}

void wedge(mc_t *c, float x0, float y0, float z0, float x1, float z1, float h0, float h1, int m)
{
    if (h0 <= 0) {
        const float uv[6] = { y0, z0, y0 + h1, z1, y0, z1 };
        prism(c, 0, uv, 3, x0, x1, m);
    } else if (h1 <= 0) {
        const float uv[6] = { y0, z0, y0 + h0, z0, y0, z1 };
        prism(c, 0, uv, 3, x0, x1, m);
    } else {
        const float uv[8] = { y0, z0, y0 + h0, z0, y0 + h1, z1, y0, z1 };
        prism(c, 0, uv, 4, x0, x1, m);
    }
}

void mirror_x(mc_t *c, int from)
{
    mesh_model_t *M = c->m;
    int n = M->nfaces;
    for (int i = from; i < n && M->nfaces < MESH_MAX_FACES; i++) {
        const mesh_face_t *s = &M->faces[i];
        mesh_face_t *f = &M->faces[M->nfaces++];
        f->n = s->n;
        f->c = s->c;
        for (int k = 0; k < s->n; k++) {
            int j = s->n - 1 - k;
            f->p[k][0] = -s->p[j][0];
            f->p[k][1] = s->p[j][1];
            f->p[k][2] = s->p[j][2];
            f->b[k] = (uint8_t)c->bmir[s->b[j]];
        }
    }
}

/* ---------------------------------------------------------------- bones and poses */

int bone(mc_t *c, const char *name, int parent, float hx, float hy, float hz, float tx, float ty, float tz)
{
    mesh_model_t *M = c->m;
    if (M->nbones >= MESH_MAX_BONES)
        return c->bone;
    int i = M->nbones++;
    mesh_bone_t *b = &M->bones[i];
    memset(b->name, 0, sizeof b->name);
    strncpy(b->name, name, 15);
    b->parent = parent < 0 || parent >= i ? -1 : parent;
    b->head[0] = hx; b->head[1] = hy; b->head[2] = hz;
    b->tail[0] = tx; b->tail[1] = ty; b->tail[2] = tz;
    c->bmir[i] = i;
    c->bone = i;
    return i;
}

static void mirror_name(const char *s, char *o)
{
    size_t n = strlen(s);
    memcpy(o, s, n + 1);
    if (n >= 2 && s[n - 2] == '.' && (s[n - 1] == 'L' || s[n - 1] == 'R'))
        o[n - 1] = s[n - 1] == 'L' ? 'R' : 'L';
    else if (n < 14)
        strcpy(o + n, ".R");
}

int bone_mirror(mc_t *c, int i)
{
    mesh_model_t *M = c->m;
    if (i < 0 || i >= M->nbones || M->nbones >= MESH_MAX_BONES)
        return i;
    const mesh_bone_t *s = &M->bones[i];
    char name[16];
    mirror_name(s->name, name);
    int parent = s->parent < 0 ? -1 : c->bmir[s->parent];
    int n0 = M->nbones;
    int j = bone(c, name, parent, -s->head[0], s->head[1], s->head[2], -s->tail[0], s->tail[1], s->tail[2]);
    c->bmir[i] = j;
    c->bmir[j] = i;
    for (int k = 0; k < n0; k++)
        if (M->bones[k].parent == i)
            bone_mirror(c, k);
    c->bone = j;
    return j;
}

void use(mc_t *c, int b)
{
    c->bone = b < 0 ? 0 : b >= c->m->nbones ? (c->m->nbones ? c->m->nbones - 1 : 0) : b;
}

mesh_clip_t *clip(mc_t *c, const char *name, float length, int loop)
{
    mesh_model_t *M = c->m;
    static mesh_clip_t spare;
    if (M->nclips >= MESH_MAX_CLIPS)
        return &spare;
    mesh_clip_t *k = &M->clips[M->nclips++];
    memset(k, 0, sizeof *k);
    strncpy(k->name, name, 15);
    k->length = length;
    k->loop = loop;
    return k;
}

mesh_key_t *key(mesh_clip_t *k, float t)
{
    static mesh_key_t spare;
    mesh_key_t *o = k->nkeys < MESH_MAX_KEYS ? &k->keys[k->nkeys++] : &spare;
    memset(o, 0, sizeof *o);
    o->t = t;
    for (int i = 0; i < MESH_MAX_BONES; i++)
        o->pose[i].q[3] = 1;
    return o;
}

static void qmul(const float *a, const float *b, float *o)
{
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void qaxis(float x, float y, float z, float deg, float *o)
{
    float a = deg * PI_F / 180.0f, s = sinf(a / 2);
    o[0] = x * s;
    o[1] = y * s;
    o[2] = z * s;
    o[3] = cosf(a / 2);
}

/* degrees around x, then y, then z (R = Rz Ry Rx, as rig.js fromEuler) */
void turn(mesh_key_t *k, int b, float rx, float ry, float rz)
{
    if (b < 0 || b >= MESH_MAX_BONES)
        return;
    float qx[4], qy[4], qz[4], t[4];
    qaxis(1, 0, 0, rx, qx);
    qaxis(0, 1, 0, ry, qy);
    qaxis(0, 0, 1, rz, qz);
    qmul(qz, qy, t);
    qmul(t, qx, k->pose[b].q);
}

void shift(mesh_key_t *k, int b, float x, float y, float z)
{
    if (b < 0 || b >= MESH_MAX_BONES)
        return;
    k->pose[b].t[0] = x;
    k->pose[b].t[1] = y;
    k->pose[b].t[2] = z;
}

/* ---------------------------------------------------------------- the request */

void mesh_req_init(mesh_req_t *r, const char *gen)
{
    memset(r, 0, sizeof *r);
    r->gen = gen;
    r->seed = 1;
    r->color[0] = r->color[1] = MESH_NO_COLOR;
    r->scale = r->tall = r->wide = 1;
    r->rig = 1;
}

static int is(const char *w, const char *list)
{
    /* list: words separated by spaces */
    size_t n = strlen(w);
    for (const char *p = list; *p;) {
        const char *e = strchr(p, ' ');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !memcmp(p, w, n))
            return 1;
        p += len + (e ? 1 : 0);
    }
    return 0;
}

void mesh_parse(const char *text, mesh_req_t *r)
{
    static ai_words_t ws;
    ai_words(text, AI_MAX_TOKENS, &ws);
    int nc = 0;
    for (int i = 0; i < ws.n; i++) {
        const char *w = ws.w[i];
        uint32_t col = spr_color_word(w);
        if (col != SPR_NO_COLOR && nc < 2) {
            if (!(nc == 1 && r->color[0] == col))
                r->color[nc++] = col;
        }
        if (is(w, "piccolo piccola piccoli small tiny mini minuscolo"))
            r->scale = 0.6f;
        else if (is(w, "grande big large grosso grossa"))
            r->scale = 1.5f;
        else if (is(w, "enorme huge gigante giant giganti gigantesco immenso"))
            r->scale = 2.5f;
        else if (is(w, "alto alta alti alte tall high"))
            r->tall = 1.3f;
        else if (is(w, "basso bassa bassi basse short low"))
            r->tall = 0.75f;
        else if (is(w, "largo larga larghi wide fat"))
            r->wide = 1.3f;
        else if (is(w, "stretto stretta sottile thin narrow slim magro magra snello"))
            r->wide = 0.75f;
        if (is(w, "senza no without") && i + 1 < ws.n &&
            is(ws.w[i + 1], "scheletro skeleton ossa bones rig animazioni animazione animations animation"))
            r->rig = 0;
        if (is(w, "fermo ferma statico statica static"))
            r->rig = 0;
    }
}

/* ---------------------------------------------------------------- shapes */

static void r_cube(mc_t *c)
{
    mat(c, 1, mpick(c, MP_BRIGHT, 8));
    bx(c, 0, 0, 0, 1, 1, 1, 1);
}

static void r_sphere(mc_t *c)
{
    mat(c, 1, mpick(c, MP_BRIGHT, 8));
    ell(c, 0, 0.5f, 0, 0.5f, 0.5f, 0.5f, 8, 1);
}

static void r_cylinder(mc_t *c)
{
    mat(c, 1, mpick(c, MP_BRIGHT, 8));
    cyl(c, 0, 0, 0, 0.5f, 1, 8, 1);
}

static void r_cone(mc_t *c)
{
    mat(c, 1, mpick(c, MP_BRIGHT, 8));
    const float a[3] = { 0, 0, 0 }, b[3] = { 0, 1, 0 };
    tube(c, a, b, 0.5f, 0, 8, 1, 1);
}

static void r_pyramid(mc_t *c)
{
    mat(c, 1, 0xD8C090);
    const float a[3] = { 0, 0, 0 }, b[3] = { 0, 1, 0 };
    tube(c, a, b, 0.7071f, 0, 4, 1, 1);
}

static void r_column(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    mat(c, 2, mshade(c->col[1], 1));
    bx(c, 0, 0, 0, 1, 0.125f, 1, 2);
    cyl(c, 0, 0.125f, 0, 0.3f, 1.75f, 8, 1);
    bx(c, 0, 1.875f, 0, 1, 0.125f, 1, 2);
}

static void r_wall(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    mat(c, 2, mshade(c->col[1], 1));
    bx(c, 0, 0, 0, 4, 2, 0.5f, 1);
    if (c->seed % 2 == 0)
        for (int i = 0; i < 4; i++)                       /* battlements */
            bx(c, -1.5f + (float)i, 2, 0, 0.5f, 0.5f, 0.5f, 1);
    /* the joints of a few stones, as thin dark plates */
    for (int i = 0; i < 3; i++)
        bx(c, mrf(c, -1.5f, 1.5f), mrf(c, 0.3f, 1.5f), -0.25f, 0.6f, 0.05f, 0.02f, 2);
}

static void r_stairs(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    for (int i = 0; i < 4; i++)
        box(c, -1, 0, -0.5f + 0.25f * (float)i, 1, 0.25f * (float)(i + 1), 0.5f, 1);
}

static void r_ramp(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    wedge(c, -1, 0, -1, 1, 1, 0, 1, 1);
}

static void r_platform(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    mat(c, 2, mshade(c->col[1], 1));
    bx(c, 0, 0.25f, 0, 3, 0.25f, 2, 1);
    bx(c, 0, 0, 0, 2.6f, 0.25f, 1.6f, 2);
}

static void r_arch(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    mat(c, 2, mshade(c->col[1], 3));
    bx(c, -0.75f, 0, 0, 0.5f, 1.5f, 0.5f, 1);
    bx(c, 0.75f, 0, 0, 0.5f, 1.5f, 0.5f, 1);
    box(c, -1, 1.5f, -0.25f, 1, 2, 0.25f, 1);
    bx(c, 0, 1.5f, 0, 0.3f, 0.5f, 0.52f, 2);                  /* keystone */
}

/* ---------------------------------------------------------------- objects */

static void r_tree(mc_t *c)
{
    mat(c, 1, mpick(c, MP_LEAF, 4));
    mat(c, 2, mpick(c, MP_WOOD, 4));
    float h = mrf(c, 0.8f, 1.2f);
    cyl(c, 0, 0, 0, 0.15f, h, 6, 2);
    float r = mrf(c, 0.7f, 0.9f);
    ell(c, 0, h + r * 0.8f, 0, r, r * 0.9f, r, 8, 1);
    int lobes = mri(c, 1, 3);
    for (int i = 0; i < lobes; i++) {
        float a = mrf(c, 0, 2 * PI_F), d = r * 0.6f;
        ell(c, cosf(a) * d, h + r * mrf(c, 0.6f, 1.2f), sinf(a) * d, r * 0.55f, r * 0.5f, r * 0.55f, 6, 1);
    }
}

static void r_pine(mc_t *c)
{
    mat(c, 1, mpick(c, MP_LEAF, 4));
    mat(c, 2, mpick(c, MP_WOOD, 4));
    cyl(c, 0, 0, 0, 0.12f, 0.7f, 6, 2);
    int tiers = mri(c, 3, 4);
    float y = 0.5f;
    for (int i = 0; i < tiers; i++) {
        float r = 0.8f - 0.17f * (float)i, h = 0.8f;
        const float a[3] = { 0, y, 0 }, b[3] = { 0, y + h, 0 };
        tube(c, a, b, r, 0, 8, 1, 1);
        y += 0.45f;
    }
}

static void r_house(mc_t *c)
{
    static const uint32_t walls[] = { 0xE8D8B0, 0xD0C0A0, 0xF0E0C8, 0xC8A070 };
    static const uint32_t roofs[] = { 0xB04030, 0x6A4028, 0x707C98, 0x3A62D8 };
    mat(c, 1, mpick(c, walls, 4));
    mat(c, 2, mpick(c, roofs, 4));
    mat(c, 3, 0x6A4028);
    mat_flat(c, 4, 0x9AD8F0);
    mat(c, 5, 0x8A8A9A);
    bx(c, 0, 0, 0, 2, 1.5f, 2, 1);
    const float uv[6] = { 1.5f, -1.15f, 2.4f, 0, 1.5f, 1.15f };
    prism(c, 0, uv, 3, -1.15f, 1.15f, 2);
    box(c, -0.2f, 0, -1.03f, 0.2f, 0.7f, -0.99f, 3);               /* door */
    box(c, -0.75f, 0.7f, -1.03f, -0.4f, 1.05f, -0.99f, 4);          /* windows */
    box(c, 0.4f, 0.7f, -1.03f, 0.75f, 1.05f, -0.99f, 4);
    if (c->seed % 2)
        bx(c, 0.6f, 1.9f, 0.5f, 0.3f, 0.6f, 0.3f, 5);               /* chimney */
}

static void r_tower(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    mat(c, 2, mpick(c, (const uint32_t[]){ 0xB04030, 0x3A62D8, 0x6A4028 }, 3));
    mat_flat(c, 3, 0x302830);
    cyl(c, 0, 0, 0, 0.8f, 3, 8, 1);
    if (c->seed % 2) {
        const float a[3] = { 0, 3, 0 }, b[3] = { 0, 4.4f, 0 };
        tube(c, a, b, 0.95f, 0, 8, 2, 1);
    } else {
        for (int i = 0; i < 8; i++) {
            float t = 2 * PI_F * ((float)i + 0.5f) / 8;
            bx(c, cosf(t) * 0.72f, 3, sinf(t) * 0.72f, 0.3f, 0.35f, 0.3f, 1);
        }
    }
    box(c, -0.12f, 1.6f, -0.85f, 0.12f, 2.0f, -0.75f, 3);           /* a window */
    box(c, -0.2f, 0, -0.85f, 0.2f, 0.6f, -0.75f, 3);                /* the door */
}

static void r_chair(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, mpick(c, MP_CLOTH, 6));
    bx(c, 0, 0.45f, 0, 0.6f, 0.08f, 0.6f, 1);
    bx(c, 0, 0.53f, 0, 0.5f, 0.05f, 0.5f, 2);                       /* the cushion */
    for (int i = 0; i < 4; i++)
        cyl(c, (i & 1) ? 0.25f : -0.25f, 0, (i & 2) ? 0.25f : -0.25f, 0.04f, 0.45f, 4, 1);
    bx(c, 0, 0.53f, 0.27f, 0.6f, 0.65f, 0.06f, 1);
}

static void r_table(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    bx(c, 0, 0.72f, 0, 1.6f, 0.08f, 0.9f, 1);
    for (int i = 0; i < 4; i++)
        cyl(c, (i & 1) ? 0.7f : -0.7f, 0, (i & 2) ? 0.35f : -0.35f, 0.05f, 0.72f, 4, 1);
}

static void r_barrel(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, 0x50545C);
    const float p0[3] = { 0, 0, 0 }, p1[3] = { 0, 0.3f, 0 }, p2[3] = { 0, 0.7f, 0 }, p3[3] = { 0, 1, 0 };
    tube(c, p0, p1, 0.33f, 0.4f, 8, 1, 1);
    tube(c, p1, p2, 0.4f, 0.4f, 8, 1, 0);
    tube(c, p2, p3, 0.4f, 0.33f, 8, 1, 1);
    const float b0[3] = { 0, 0.25f, 0 }, b1[3] = { 0, 0.33f, 0 }, b2[3] = { 0, 0.67f, 0 }, b3[3] = { 0, 0.75f, 0 };
    tube(c, b0, b1, 0.41f, 0.42f, 8, 2, 0);
    tube(c, b2, b3, 0.42f, 0.41f, 8, 2, 0);
}

static void r_crate(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, mshade(c->col[1], 1));
    bx(c, 0, 0, 0, 1, 1, 1, 1);
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? 0.47f : -0.47f, z = (i & 2) ? 0.47f : -0.47f;
        bx(c, x, 0, z, 0.1f, 1, 0.1f, 2);                           /* corner posts */
        bx(c, x, 0.45f, 0, 0.1f, 0.1f, 1.02f, 2);                   /* side bands */
        bx(c, 0, 0.45f, z, 1.02f, 0.1f, 0.1f, 2);
    }
    box(c, -0.5f, 0.95f, -0.05f, 0.5f, 1.02f, 0.05f, 2);
    box(c, -0.05f, 0.95f, -0.5f, 0.05f, 1.02f, 0.5f, 2);
}

static void r_chest(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, 0xE8B830);
    mat(c, 3, 0x50545C);
    bx(c, 0, 0, 0, 1, 0.5f, 0.7f, 1);
    const float lid[12] = { -0.5f, 0.5f, -0.5f, 0.68f, -0.25f, 0.84f, 0.25f, 0.84f, 0.5f, 0.68f, 0.5f, 0.5f };
    prism(c, 2, lid, 6, -0.35f, 0.35f, 1);
    bx(c, -0.35f, 0, 0, 0.08f, 0.52f, 0.72f, 3);
    bx(c, 0.35f, 0, 0, 0.08f, 0.52f, 0.72f, 3);
    bx(c, 0, 0.4f, -0.36f, 0.16f, 0.2f, 0.06f, 2);                 /* the lock */
}

static void r_sword(mc_t *c)
{
    mat(c, 1, mpick(c, MP_METAL, 4));
    mat(c, 2, 0xE8B830);
    mat(c, 3, 0x6A4028);
    const float blade[8] = { -0.07f, 0, 0, 0.025f, 0.07f, 0, 0, -0.025f };
    prism(c, 1, blade, 4, 0.35f, 1.45f, 1);
    const float a[3] = { 0, 1.45f, 0 }, b[3] = { 0, 1.7f, 0 };
    tube(c, a, b, 0.07f, 0, 4, 1, 0);
    bx(c, 0, 0.28f, 0, 0.42f, 0.07f, 0.1f, 2);
    cyl(c, 0, 0.05f, 0, 0.045f, 0.23f, 6, 3);
    ell(c, 0, 0.03f, 0, 0.07f, 0.05f, 0.07f, 6, 2);
}

static void r_shield(mc_t *c)
{
    mat(c, 1, mpick(c, MP_CLOTH, 6));
    mat(c, 2, mpick(c, MP_METAL, 4));
    const float heater[12] = { -0.45f, 1.1f, 0.45f, 1.1f, 0.45f, 0.6f, 0, 0.1f, -0.45f, 0.6f, -0.45f, 1.1f };
    prism(c, 2, heater, 5, -0.04f, 0.04f, 1);
    const float rim[12] = { -0.5f, 1.15f, 0.5f, 1.15f, 0.5f, 0.6f, 0, 0.05f, -0.5f, 0.6f, -0.5f, 1.15f };
    prism(c, 2, rim, 5, 0.04f, 0.08f, 2);
    ell(c, 0, 0.72f, -0.05f, 0.12f, 0.12f, 0.08f, 6, 2);
}

static void r_car(mc_t *c)
{
    mat(c, 1, mpick(c, MP_BRIGHT, 8));
    mat_flat(c, 2, 0x9AD8F0);
    mat(c, 3, 0x30323A);
    mat_flat(c, 4, 0xFFF0A0);
    mat_flat(c, 5, 0xE03030);
    mat(c, 6, 0xC0C8D8);
    bx(c, 0, 0.22f, 0, 1.0f, 0.4f, 2.0f, 1);
    bx(c, 0, 0.62f, 0.1f, 0.84f, 0.38f, 1.0f, 1);
    box(c, -0.4f, 0.68f, -0.42f, 0.4f, 0.96f, -0.38f, 2);           /* windscreen */
    box(c, -0.4f, 0.68f, 0.58f, 0.4f, 0.96f, 0.62f, 2);
    box(c, -0.44f, 0.68f, -0.3f, -0.4f, 0.96f, 0.5f, 2);
    box(c, 0.4f, 0.68f, -0.3f, 0.44f, 0.96f, 0.5f, 2);
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? 0.46f : -0.46f, z = (i & 2) ? 0.65f : -0.65f;
        const float a[3] = { x - 0.1f, 0.22f, z }, b[3] = { x + 0.1f, 0.22f, z };
        tube(c, a, b, 0.22f, 0.22f, 8, 3, 1);
    }
    box(c, -0.42f, 0.3f, -1.02f, -0.22f, 0.44f, -0.98f, 4);         /* lights */
    box(c, 0.22f, 0.3f, -1.02f, 0.42f, 0.44f, -0.98f, 4);
    box(c, -0.42f, 0.3f, 0.98f, -0.22f, 0.44f, 1.02f, 5);
    box(c, 0.22f, 0.3f, 0.98f, 0.42f, 0.44f, 1.02f, 5);
    bx(c, 0, 0.12f, -1.0f, 1.04f, 0.12f, 0.08f, 6);                 /* bumpers */
    bx(c, 0, 0.12f, 1.0f, 1.04f, 0.12f, 0.08f, 6);
}

static void r_lamp(mc_t *c)
{
    mat(c, 1, 0x30323A);
    mat_flat(c, 2, 0xFFE880);
    cyl(c, 0, 0, 0, 0.14f, 0.15f, 6, 1);
    cyl(c, 0, 0.15f, 0, 0.05f, 2.1f, 6, 1);
    bx(c, 0, 2.2f, 0, 0.3f, 0.06f, 0.3f, 1);
    bx(c, 0, 2.26f, 0, 0.22f, 0.3f, 0.22f, 2);
    const float a[3] = { 0, 2.56f, 0 }, b[3] = { 0, 2.78f, 0 };
    tube(c, a, b, 0.2f, 0.04f, 4, 1, 1);
}

static void r_fence(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    for (int i = 0; i < 3; i++) {
        bx(c, -1 + (float)i, 0, 0, 0.12f, 1.0f, 0.12f, 1);
        const float a[3] = { -1 + (float)i, 1.0f, 0 }, b[3] = { -1 + (float)i, 1.12f, 0 };
        tube(c, a, b, 0.09f, 0, 4, 1, 0);
    }
    bx(c, 0, 0.3f, 0, 2.1f, 0.1f, 0.05f, 1);
    bx(c, 0, 0.7f, 0, 2.1f, 0.1f, 0.05f, 1);
}

static void r_rock(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    float rx = mrf(c, 0.5f, 0.8f), ry = mrf(c, 0.35f, 0.55f), rz = mrf(c, 0.45f, 0.7f);
    ell(c, 0, ry * 0.8f, 0, rx, ry, rz, mri(c, 5, 7), 1);
    ell(c, mrf(c, -0.4f, 0.4f), ry * 0.5f, mrf(c, -0.3f, 0.3f), rx * 0.5f, ry * 0.6f, rz * 0.5f, 5, 1);
}

static void r_bush(mc_t *c)
{
    mat(c, 1, mpick(c, MP_LEAF, 4));
    ell(c, 0, 0.45f, 0, 0.6f, 0.45f, 0.55f, 7, 1);
    ell(c, 0.35f, 0.35f, 0.2f, 0.4f, 0.35f, 0.35f, 6, 1);
    ell(c, -0.3f, 0.4f, -0.15f, 0.35f, 0.4f, 0.35f, 6, 1);
    if (c->seed % 2) {
        mat_flat(c, 2, 0xE03030);
        for (int i = 0; i < 4; i++)
            bx(c, mrf(c, -0.4f, 0.4f), mrf(c, 0.3f, 0.8f), mrf(c, -0.5f, -0.3f), 0.08f, 0.08f, 0.08f, 2);
    }
}

static void r_mushroom(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0xD83A3A, 0xF08A30, 0x8A4AD0, 0xC8A070 }, 4));
    mat(c, 2, 0xF0E0C8);
    mat_flat(c, 3, 0xF8F8F0);
    cyl(c, 0, 0, 0, 0.2f, 0.6f, 6, 2);
    ell(c, 0, 0.6f, 0, 0.6f, 0.32f, 0.6f, 8, 1);
    for (int i = 0; i < 4; i++) {
        float t = mrf(c, 0, 2 * PI_F), d = mrf(c, 0.1f, 0.4f);
        bx(c, cosf(t) * d, 0.78f + (0.3f - d) * 0.3f, sinf(t) * d, 0.12f, 0.06f, 0.12f, 3);
    }
}

static void r_cactus(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x3C9060, 0x2E8A3C, 0x58B848 }, 3));
    mat(c, 2, 0xC8A070);
    bx(c, 0, 0, 0, 0.9f, 0.1f, 0.9f, 2);
    cyl(c, 0, 0.05f, 0, 0.25f, 1.6f, 8, 1);
    ell(c, 0, 1.65f, 0, 0.25f, 0.15f, 0.25f, 8, 1);
    int arms = mri(c, 1, 2);
    for (int i = 0; i < arms; i++) {
        float s = i ? -1 : 1, y = mrf(c, 0.6f, 0.9f);
        const float a[3] = { s * 0.2f, y, 0 }, b[3] = { s * 0.55f, y, 0 }, d[3] = { s * 0.55f, y + 0.6f, 0 };
        tube(c, a, b, 0.14f, 0.14f, 6, 1, 0);
        tube(c, b, d, 0.14f, 0.14f, 6, 1, 0);
        ell(c, s * 0.55f, y + 0.6f, 0, 0.14f, 0.1f, 0.14f, 6, 1);
    }
}

static void r_bridge(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, mshade(c->col[1], 1));
    for (int i = 0; i < 6; i++)
        box(c, -0.6f, 0.4f, -1.5f + 0.5f * (float)i, 0.6f, 0.5f, -1.05f + 0.5f * (float)i, 1);
    for (int s = -1; s <= 1; s += 2) {
        box(c, s * 0.55f - 0.04f, 0.4f, -1.5f, s * 0.55f + 0.04f, 0.5f, 1.5f, 2);
        for (int i = 0; i < 3; i++)
            bx(c, s * 0.62f, 0.5f, -1.4f + 1.4f * (float)i, 0.08f, 0.6f, 0.08f, 1);
        bx(c, s * 0.62f, 1.05f, 0, 0.06f, 0.06f, 3.0f, 1);
    }
    bx(c, 0, 0, -1.3f, 1.3f, 0.4f, 0.3f, 2);
    bx(c, 0, 0, 1.3f, 1.3f, 0.4f, 0.3f, 2);
}

static void r_well(mc_t *c)
{
    mat(c, 1, mpick(c, MP_STONE, 4));
    mat(c, 2, mpick(c, MP_WOOD, 4));
    mat(c, 3, 0xB04030);
    mat_flat(c, 4, 0x204060);
    cyl(c, 0, 0, 0, 0.55f, 0.6f, 8, 1);
    cyl(c, 0, 0.55f, 0, 0.4f, 0.07f, 8, 4);
    bx(c, -0.5f, 0.6f, 0, 0.1f, 1.0f, 0.1f, 2);
    bx(c, 0.5f, 0.6f, 0, 0.1f, 1.0f, 0.1f, 2);
    const float uv[6] = { 1.55f, -0.5f, 2.0f, 0, 1.55f, 0.5f };
    prism(c, 0, uv, 3, -0.7f, 0.7f, 3);
    const float a[3] = { -0.5f, 1.4f, 0 }, b[3] = { 0.5f, 1.4f, 0 };
    tube(c, a, b, 0.05f, 0.05f, 6, 2, 0);
    cyl(c, 0, 0.9f, 0, 0.1f, 0.15f, 6, 2);                          /* the bucket */
}

static void r_sign(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, 0xE8D8B0);
    mat_flat(c, 3, 0x40302A);
    cyl(c, 0, 0, 0, 0.06f, 1.5f, 6, 1);
    bx(c, 0, 1.0f, 0, 1.0f, 0.4f, 0.08f, 2);
    for (int i = 0; i < 2; i++)
        box(c, -0.4f, 1.1f + 0.14f * (float)i, -0.045f, -0.4f + mrf(c, 0.4f, 0.8f), 1.16f + 0.14f * (float)i, -0.04f, 3);
    if (c->seed % 2) {
        const float arrow[6] = { 1.0f, -0.04f, 1.2f, 0.0f, 1.4f, -0.04f };
        prism(c, 2, arrow, 3, 0.5f, 0.7f, 2);
    }
}

static void r_torch(mc_t *c)
{
    mat(c, 1, 0x6A4028);
    mat_flat(c, 2, 0xF08A30);
    mat_flat(c, 3, 0xFFE060);
    cyl(c, 0, 0, 0, 0.05f, 0.7f, 6, 1);
    cyl(c, 0, 0.7f, 0, 0.09f, 0.15f, 6, 1);
    ell(c, 0, 1.0f, 0, 0.14f, 0.2f, 0.14f, 6, 2);
    ell(c, 0, 1.0f, 0, 0.07f, 0.11f, 0.07f, 4, 3);
}

static void r_boat(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, 0xF0F0F0);
    mat(c, 3, mshade(c->col[1], 1));
    const float bow[3] = { 0, 0.4f, -1.3f }, f[3] = { 0, 0.25f, -0.5f }, bk[3] = { 0, 0.25f, 0.5f }, st[3] = { 0, 0.35f, 1.1f };
    tube(c, bow, f, 0, 0.42f, 6, 1, 0);
    tube(c, f, bk, 0.42f, 0.42f, 6, 1, 0);
    tube(c, bk, st, 0.42f, 0.3f, 6, 1, 1);
    box(c, -0.3f, 0.42f, -0.5f, 0.3f, 0.46f, 0.5f, 3);
    bx(c, 0, 0.3f, 0.2f, 0.7f, 0.08f, 0.15f, 3);
    if (c->seed % 2) {
        cyl(c, 0, 0.3f, -0.1f, 0.04f, 1.6f, 6, 1);
        const float sail[6] = { 0.04f, 0.6f, 0.04f, 1.75f, 0.7f, 0.6f };
        prism(c, 2, sail, 3, -0.12f, -0.1f, 2);
    }
}

static void r_cannon(mc_t *c)
{
    mat(c, 1, 0x30323A);
    mat(c, 2, mpick(c, MP_WOOD, 4));
    mat(c, 3, 0x50545C);
    const float a[3] = { 0, 0.5f, 0.5f }, b[3] = { 0, 0.55f, -0.9f };
    tube(c, a, b, 0.22f, 0.16f, 8, 1, 1);
    const float m0[3] = { 0, 0.55f, -0.95f }, m1[3] = { 0, 0.55f, -0.8f };
    tube(c, m0, m1, 0.2f, 0.2f, 8, 1, 1);
    ell(c, 0, 0.5f, 0.55f, 0.2f, 0.2f, 0.2f, 6, 1);
    bx(c, 0, 0.1f, 0.1f, 0.5f, 0.35f, 1.2f, 2);
    for (int s = -1; s <= 1; s += 2) {
        const float w0[3] = { s * 0.3f, 0.3f, 0.2f }, w1[3] = { s * 0.4f, 0.3f, 0.2f };
        tube(c, w0, w1, 0.3f, 0.3f, 8, 3, 1);
    }
}

static void r_bed(mc_t *c)
{
    mat(c, 1, mpick(c, MP_WOOD, 4));
    mat(c, 2, mpick(c, MP_CLOTH, 6));
    mat(c, 3, 0xF0F0F0);
    bx(c, 0, 0.2f, 0, 1.2f, 0.2f, 2.2f, 1);
    for (int i = 0; i < 4; i++)
        bx(c, (i & 1) ? 0.55f : -0.55f, 0, (i & 2) ? 1.05f : -1.05f, 0.1f, 0.2f, 0.1f, 1);
    bx(c, 0, 0.4f, 0, 1.1f, 0.15f, 2.1f, 3);
    bx(c, 0, 0.55f, 0.15f, 1.1f, 0.12f, 1.6f, 2);
    bx(c, 0, 0.55f, -0.8f, 0.8f, 0.14f, 0.35f, 3);                  /* the pillow */
    bx(c, 0, 0.2f, -1.12f, 1.2f, 0.8f, 0.08f, 1);
}

static void r_flower(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0xD83A3A, 0xF080B0, 0xF0D040, 0x8A4AD0, 0xF0F0F0 }, 5));
    mat(c, 2, 0x3CA048);
    mat_flat(c, 3, 0xF0D040);
    cyl(c, 0, 0, 0, 0.035f, 0.9f, 5, 2);
    ell(c, 0.18f, 0.35f, 0, 0.2f, 0.04f, 0.1f, 5, 2);
    ell(c, -0.15f, 0.5f, 0.05f, 0.18f, 0.04f, 0.09f, 5, 2);
    int n = mri(c, 5, 6);
    for (int i = 0; i < n; i++) {
        float t = 2 * PI_F * (float)i / (float)n;
        ell(c, cosf(t) * 0.18f, 0.92f, sinf(t) * 0.18f, 0.14f, 0.05f, 0.1f, 5, 1);
    }
    ell(c, 0, 0.94f, 0, 0.1f, 0.07f, 0.1f, 6, 3);
}

static void r_coin(mc_t *c)
{
    mat(c, 1, 0xE8B830);
    mat(c, 2, 0xF8E090);
    const float a[3] = { 0, 0.5f, -0.05f }, b[3] = { 0, 0.5f, 0.05f };
    tube(c, a, b, 0.5f, 0.5f, 8, 1, 1);
    const float a2[3] = { 0, 0.5f, -0.06f }, b2[3] = { 0, 0.5f, 0.06f };
    tube(c, a2, b2, 0.32f, 0.32f, 8, 2, 1);
}

static void r_gem(mc_t *c)
{
    mat(c, 1, mpick(c, (const uint32_t[]){ 0x3A62D8, 0xD83A3A, 0x3CB043, 0x8A4AD0, 0x48B8E8 }, 5));
    const float t[3] = { 0, 1, 0 }, m[3] = { 0, 0.6f, 0 }, b[3] = { 0, 0, 0 };
    tube(c, m, t, 0.4f, 0.15f, 6, 1, 1);
    tube(c, m, b, 0.4f, 0, 6, 1, 0);
}

/* ---------------------------------------------------------------- the recipes */

typedef struct {
    const char *id, *name;
    void (*fn)(mc_t *);
    int rigged;
} recipe_t;

static const recipe_t recipes[] = {
    { "cube", "cube", r_cube, 0 },
    { "sphere", "sphere", r_sphere, 0 },
    { "cylinder", "cylinder", r_cylinder, 0 },
    { "cone", "cone", r_cone, 0 },
    { "pyramid", "pyramid", r_pyramid, 0 },
    { "column", "column", r_column, 0 },
    { "wall", "wall", r_wall, 0 },
    { "stairs", "stairs", r_stairs, 0 },
    { "ramp", "ramp", r_ramp, 0 },
    { "platform", "platform", r_platform, 0 },
    { "arch", "arch", r_arch, 0 },
    { "tree", "tree", r_tree, 0 },
    { "pine", "pine tree", r_pine, 0 },
    { "house", "house", r_house, 0 },
    { "tower", "tower", r_tower, 0 },
    { "chair", "chair", r_chair, 0 },
    { "table", "table", r_table, 0 },
    { "barrel", "barrel", r_barrel, 0 },
    { "crate", "crate", r_crate, 0 },
    { "chest", "chest", r_chest, 0 },
    { "sword", "sword", r_sword, 0 },
    { "shield", "shield", r_shield, 0 },
    { "car", "car", r_car, 0 },
    { "lamp", "lamp post", r_lamp, 0 },
    { "fence", "fence", r_fence, 0 },
    { "rock", "rock", r_rock, 0 },
    { "bush", "bush", r_bush, 0 },
    { "mushroom", "mushroom", r_mushroom, 0 },
    { "cactus", "cactus", r_cactus, 0 },
    { "bridge", "bridge", r_bridge, 0 },
    { "well", "well", r_well, 0 },
    { "sign", "signpost", r_sign, 0 },
    { "torch", "torch", r_torch, 0 },
    { "boat", "boat", r_boat, 0 },
    { "cannon", "cannon", r_cannon, 0 },
    { "bed", "bed", r_bed, 0 },
    { "flower", "flower", r_flower, 0 },
    { "coin", "coin", r_coin, 0 },
    { "gem", "gem", r_gem, 0 },
    { "hero", "person", r_hero, 1 },
    { "knight", "knight", r_knight, 1 },
    { "robot", "robot", r_robot, 1 },
    { "mech", "mech", r_mech, 1 },
    { "dog", "dog", r_dog, 1 },
    { "horse", "horse", r_horse, 1 },
    { "bird", "bird", r_bird, 1 },
    { "fish", "fish", r_fish, 1 },
    { "slime", "slime", r_slime, 1 },
    { "spider", "spider", r_spider, 1 },
    { "dragon", "dragon", r_dragon, 1 },
    { "ghost", "ghost", r_ghost, 1 },
    { "skeleton", "skeleton", r_skeleton, 1 },
    { "snowman", "snowman", r_snowman, 1 },
};

#define NRECIPES ((int)(sizeof recipes / sizeof recipes[0]))

int mesh_recipes(void) { return NRECIPES; }
const char *mesh_recipe_id(int i) { return i >= 0 && i < NRECIPES ? recipes[i].id : ""; }
const char *mesh_recipe_name(int i) { return i >= 0 && i < NRECIPES ? recipes[i].name : ""; }
int mesh_recipe_rigged(int i) { return i >= 0 && i < NRECIPES ? recipes[i].rigged : 0; }

int mesh_find(const char *gen)
{
    if (!gen)
        return -1;
    for (int i = 0; i < NRECIPES; i++)
        if (!strcmp(gen, recipes[i].id))
            return i;
    return -1;
}

static float snap(float v) { return floorf(v * 256.0f + 0.5f) / 256.0f; }

int mesh_make(const mesh_req_t *r, mesh_model_t *out)
{
    int i = mesh_find(r->gen);
    if (i < 0)
        return -1;
    static mc_t c;
    memset(&c, 0, sizeof c);
    memset(out, 0, sizeof *out);
    c.m = out;
    c.seed = r->seed ? r->seed : 1;
    c.rng = c.seed * 2654435761u + 12345u;
    for (int k = 0; k < 4; k++) mrnd(&c);
    c.tall = r->tall > 0 ? r->tall : 1;
    c.wide = r->wide > 0 ? r->wide : 1;
    c.rig = r->rig;
    memset(locked, 0, sizeof locked);
    for (int k = 0; k < NMAT; k++) c.col[k] = 0x8A8A9A;
    for (int k = 0; k < 2; k++)
        if (r->color[k] != MESH_NO_COLOR) {
            c.col[k + 1] = r->color[k];
            locked[k + 1] = 1;
        }
    recipes[i].fn(&c);
    if (!r->rig) {
        out->nbones = 0;
        out->nclips = 0;
        for (int f = 0; f < out->nfaces; f++)
            memset(out->faces[f].b, 0, 4);
    }
    /* the request's size and proportions, then a grid of 1/256 */
    float s = r->scale > 0 ? r->scale : 1;
    float sx = s * c.wide, sy = s * c.tall, sz = s * c.wide;
    for (int f = 0; f < out->nfaces; f++)
        for (int k = 0; k < 4; k++) {
            out->faces[f].p[k][0] = snap(out->faces[f].p[k][0] * sx);
            out->faces[f].p[k][1] = snap(out->faces[f].p[k][1] * sy);
            out->faces[f].p[k][2] = snap(out->faces[f].p[k][2] * sz);
        }
    for (int b = 0; b < out->nbones; b++) {
        mesh_bone_t *bn = &out->bones[b];
        bn->head[0] = snap(bn->head[0] * sx); bn->head[1] = snap(bn->head[1] * sy); bn->head[2] = snap(bn->head[2] * sz);
        bn->tail[0] = snap(bn->tail[0] * sx); bn->tail[1] = snap(bn->tail[1] * sy); bn->tail[2] = snap(bn->tail[2] * sz);
    }
    for (int k = 0; k < out->nclips; k++)
        for (int j = 0; j < out->clips[k].nkeys; j++)
            for (int b = 0; b < out->nbones; b++) {
                float *t = out->clips[k].keys[j].pose[b].t;
                t[0] *= sx; t[1] *= sy; t[2] *= sz;
            }
    return 0;
}
