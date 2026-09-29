#include "r3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR 0.1f

/* ---------------------------------------------------------------- raster */

typedef struct { float x, y, z; } sv_t;     /* screen x, y; z = 1/depth */

/*
 * Scanline rasterizer for one triangle. Pixel centres are sampled at
 * (x + 0.5, y + 0.5) so shared edges are drawn once. With zbuf == NULL
 * it is a plain 2D fill.
 */
static uint32_t raster(g16_t *g, uint16_t *zbuf, sv_t a, sv_t b, sv_t c, uint16_t color)
{
    sv_t t;
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (b.y > c.y) { t = b; b = c; c = t; }
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (c.y - a.y < 1e-6f)
        return 0;

    int y0 = (int)ceilf(a.y - 0.5f), y1 = (int)ceilf(c.y - 0.5f);
    if (y0 < g->cy0) y0 = g->cy0;
    if (y1 > g->cy1) y1 = g->cy1;
    uint32_t count = 0;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        /* long edge a-c, short edge a-b or b-c */
        float tl = (py - a.y) / (c.y - a.y);
        float xl = a.x + (c.x - a.x) * tl, zl = a.z + (c.z - a.z) * tl;
        float xr, zr;
        if (py < b.y) {
            float ts = (py - a.y) / (b.y - a.y);
            xr = a.x + (b.x - a.x) * ts; zr = a.z + (b.z - a.z) * ts;
        } else {
            float d = c.y - b.y;
            float ts = d > 1e-6f ? (py - b.y) / d : 1.0f;
            xr = b.x + (c.x - b.x) * ts; zr = b.z + (c.z - b.z) * ts;
        }
        if (xl > xr) { float s = xl; xl = xr; xr = s; s = zl; zl = zr; zr = s; }

        int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f);
        if (x0 < g->cx0) x0 = g->cx0;
        if (x1 > g->cx1) x1 = g->cx1;
        if (x0 >= x1)
            continue;

        uint16_t *row = g->px + (uint32_t)y * g->stride;
        if (!zbuf) {
            for (int x = x0; x < x1; x++)
                row[x] = color;
            count += (uint32_t)(x1 - x0);
            continue;
        }
        /* z = 1/depth, scaled to 16 bits: bigger = nearer */
        float dz = (xr - xl) > 1e-6f ? (zr - zl) / (xr - xl) : 0.0f;
        float z = zl + (x0 + 0.5f - xl) * dz;
        int32_t zf = (int32_t)(z * 65535.0f * 256.0f), dzf = (int32_t)(dz * 65535.0f * 256.0f);
        uint16_t *zrow = zbuf + (uint32_t)y * g->w;
        for (int x = x0; x < x1; x++, zf += dzf) {
            int32_t zz = zf >> 8;
            if (zz > 65535) zz = 65535;
            if (zz > zrow[x]) {
                zrow[x] = (uint16_t)zz;
                row[x] = color;
                count++;
            }
        }
    }
    return count;
}

/* 4x4 ordered dither, 0..15: the colour steps of RGB565 (8 levels of red
 * and blue, 4 of green) become a fine pattern instead of bands. */
static const uint8_t bayer4[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static inline uint16_t dither565(int32_t r, int32_t g, int32_t b, uint32_t d)
{
    /* r, g, b: 0..255 (may overshoot slightly); d: 0..15 */
    r += (int32_t)(d >> 1); g += (int32_t)(d >> 2); b += (int32_t)(d >> 1);
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

/* Gouraud triangle: the colour (r, g, b in 0..255) is interpolated across
 * the face, in screen space, and dithered. With zbuf == NULL, a 2D fill. */
typedef struct { float x, y, z, r, g, b; } gv_t;

static uint32_t raster_gouraud(g16_t *g, uint16_t *zbuf, gv_t a, gv_t b, gv_t c)
{
    gv_t s;
    if (a.y > b.y) { s = a; a = b; b = s; }
    if (b.y > c.y) { s = b; b = c; c = s; }
    if (a.y > b.y) { s = a; a = b; b = s; }
    if (c.y - a.y < 1e-6f)
        return 0;
    int y0 = (int)ceilf(a.y - 0.5f), y1 = (int)ceilf(c.y - 0.5f);
    if (y0 < g->cy0) y0 = g->cy0;
    if (y1 > g->cy1) y1 = g->cy1;
    uint32_t count = 0;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        float tl = (py - a.y) / (c.y - a.y);
        gv_t l = { a.x + (c.x - a.x) * tl, 0, a.z + (c.z - a.z) * tl,
                   a.r + (c.r - a.r) * tl, a.g + (c.g - a.g) * tl, a.b + (c.b - a.b) * tl };
        gv_t r;
        if (py < b.y) {
            float ts = (py - a.y) / (b.y - a.y);
            r = (gv_t){ a.x + (b.x - a.x) * ts, 0, a.z + (b.z - a.z) * ts,
                        a.r + (b.r - a.r) * ts, a.g + (b.g - a.g) * ts, a.b + (b.b - a.b) * ts };
        } else {
            float d = c.y - b.y;
            float ts = d > 1e-6f ? (py - b.y) / d : 1.0f;
            r = (gv_t){ b.x + (c.x - b.x) * ts, 0, b.z + (c.z - b.z) * ts,
                        b.r + (c.r - b.r) * ts, b.g + (c.g - b.g) * ts, b.b + (c.b - b.b) * ts };
        }
        if (l.x > r.x) { s = l; l = r; r = s; }
        int x0 = (int)ceilf(l.x - 0.5f), x1 = (int)ceilf(r.x - 0.5f);
        if (x0 < g->cx0) x0 = g->cx0;
        if (x1 > g->cx1) x1 = g->cx1;
        if (x0 >= x1)
            continue;
        float w = r.x - l.x, inv = w > 1e-6f ? 1.0f / w : 0.0f;
        float off = x0 + 0.5f - l.x;
        /* 16.16 fixed point across the span */
        float dr = (r.r - l.r) * inv, dg = (r.g - l.g) * inv, db = (r.b - l.b) * inv;
        int32_t cr = (int32_t)((l.r + off * dr) * 65536.0f), cg = (int32_t)((l.g + off * dg) * 65536.0f),
                cb = (int32_t)((l.b + off * db) * 65536.0f);
        int32_t ir = (int32_t)(dr * 65536.0f), ig = (int32_t)(dg * 65536.0f), ib = (int32_t)(db * 65536.0f);
        const uint8_t *dith = bayer4[y & 3];
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        if (!zbuf) {
            for (int x = x0; x < x1; x++, cr += ir, cg += ig, cb += ib)
                row[x] = dither565(cr >> 16, cg >> 16, cb >> 16, dith[x & 3]);
            count += (uint32_t)(x1 - x0);
            continue;
        }
        float dz = (r.z - l.z) * inv;
        float z = l.z + off * dz;
        int32_t zf = (int32_t)(z * 65535.0f * 256.0f), dzf = (int32_t)(dz * 65535.0f * 256.0f);
        uint16_t *zrow = zbuf + (uint32_t)y * g->w;
        for (int x = x0; x < x1; x++, zf += dzf, cr += ir, cg += ig, cb += ib) {
            int32_t zz = zf >> 8;
            if (zz > 65535) zz = 65535;
            if (zz > zrow[x]) {
                zrow[x] = (uint16_t)zz;
                row[x] = dither565(cr >> 16, cg >> 16, cb >> 16, dith[x & 3]);
                count++;
            }
        }
    }
    return count;
}

/* Textured triangle: u and v are divided by depth at the vertices and
 * interpolated with 1/z, then divided back every TEX_RUN pixels and linear in
 * between (perspective correct to a fraction of a texel, one division per run).
 * k = light at each vertex, 0..256 (the same at the three vertices for flat
 * shading). Transparent texels are skipped. */
#define TEX_RUN 16                  /* pixels between exact perspective divisions */

typedef struct { float x, y, z, u, v, k; } tv_t;    /* z = 1/depth; u, v premultiplied by z */

static uint32_t raster_tex(g16_t *g, uint16_t *zbuf, tv_t a, tv_t b, tv_t c,
                           const g16_sheet_t *t)
{
    tv_t s;
    if (a.y > b.y) { s = a; a = b; b = s; }
    if (b.y > c.y) { s = b; b = c; c = s; }
    if (a.y > b.y) { s = a; a = b; b = s; }
    if (c.y - a.y < 1e-6f)
        return 0;
    int y0 = (int)ceilf(a.y - 0.5f), y1 = (int)ceilf(c.y - 0.5f);
    if (y0 < g->cy0) y0 = g->cy0;
    if (y1 > g->cy1) y1 = g->cy1;
    uint32_t count = 0;
    const int tw = t->w, th = t->h;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        float tl = (py - a.y) / (c.y - a.y);
        tv_t l = { a.x + (c.x - a.x) * tl, 0, a.z + (c.z - a.z) * tl,
                   a.u + (c.u - a.u) * tl, a.v + (c.v - a.v) * tl, a.k + (c.k - a.k) * tl };
        tv_t r;
        if (py < b.y) {
            float ts = (py - a.y) / (b.y - a.y);
            r = (tv_t){ a.x + (b.x - a.x) * ts, 0, a.z + (b.z - a.z) * ts,
                        a.u + (b.u - a.u) * ts, a.v + (b.v - a.v) * ts, a.k + (b.k - a.k) * ts };
        } else {
            float d = c.y - b.y;
            float ts = d > 1e-6f ? (py - b.y) / d : 1.0f;
            r = (tv_t){ b.x + (c.x - b.x) * ts, 0, b.z + (c.z - b.z) * ts,
                        b.u + (c.u - b.u) * ts, b.v + (c.v - b.v) * ts, b.k + (c.k - b.k) * ts };
        }
        if (l.x > r.x) { s = l; l = r; r = s; }
        int x0 = (int)ceilf(l.x - 0.5f), x1 = (int)ceilf(r.x - 0.5f);
        if (x0 < g->cx0) x0 = g->cx0;
        if (x1 > g->cx1) x1 = g->cx1;
        if (x0 >= x1)
            continue;
        float w = r.x - l.x, inv = w > 1e-6f ? 1.0f / w : 0.0f;
        float dz = (r.z - l.z) * inv, du = (r.u - l.u) * inv, dv = (r.v - l.v) * inv;
        float off = x0 + 0.5f - l.x;
        float z = l.z + off * dz, u = l.u + off * du, v = l.v + off * dv;
        /* light in 8.8 fixed point of 0..256 */
        int32_t kf = (int32_t)((l.k + off * (r.k - l.k) * inv) * 65536.0f);
        int32_t dkf = (int32_t)((r.k - l.k) * inv * 65536.0f);
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        uint16_t *zrow = zbuf ? zbuf + (uint32_t)y * g->w : NULL;
        int32_t zf = (int32_t)(z * 65535.0f * 256.0f), dzf = (int32_t)(dz * 65535.0f * 256.0f);
        /* texel coordinates (16.16) exact every TEX_RUN pixels, linear in between */
        float iz = 1.0f / z;
        int32_t uf = (int32_t)(u * iz * 65536.0f), vf = (int32_t)(v * iz * 65536.0f);
        for (int x = x0; x < x1;) {
            int n = x1 - x < TEX_RUN ? x1 - x : TEX_RUN;
            z += dz * n; u += du * n; v += dv * n;
            float iz2 = 1.0f / (z > 1e-9f ? z : 1e-9f), in = 1.0f / n;
            int32_t uf2 = (int32_t)(u * iz2 * 65536.0f), vf2 = (int32_t)(v * iz2 * 65536.0f);
            int32_t duf = (int32_t)((float)(uf2 - uf) * in), dvf = (int32_t)((float)(vf2 - vf) * in);
            for (int e = x + n; x < e; x++, zf += dzf, uf += duf, vf += dvf, kf += dkf) {
                int32_t zz = zf >> 8;
                if (zz > 65535) zz = 65535;
                if (zrow && zz <= zrow[x])
                    continue;
                int tx = uf >> 16, ty = vf >> 16;
                if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
                if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
                uint32_t i = (uint32_t)ty * (uint32_t)tw + (uint32_t)tx;
                if (!t->alpha[i])
                    continue;
                uint32_t p = t->px[i];
                uint32_t k = kf <= 0 ? 0 : (uint32_t)(kf >> 8);
                if (k < 256) {
                    uint32_t rr = (p >> 11) * k >> 8, gg = (p >> 5 & 63) * k >> 8, bb = (p & 31) * k >> 8;
                    p = rr << 11 | gg << 5 | bb;
                }
                if (zrow)
                    zrow[x] = (uint16_t)zz;
                row[x] = (uint16_t)p;
                count++;
            }
            uf = uf2; vf = vf2;
        }
    }
    return count;
}

void g16_tri(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    sv_t a = { (float)(x0 - g->cam_x), (float)(y0 - g->cam_y), 0 };
    sv_t b = { (float)(x1 - g->cam_x), (float)(y1 - g->cam_y), 0 };
    sv_t d = { (float)(x2 - g->cam_x), (float)(y2 - g->cam_y), 0 };
    raster(g, NULL, a, b, d, c);
}

void g16_tri_gouraud(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2,
                     uint32_t c0, uint32_t c1, uint32_t c2)
{
#define GV(x, y, c) (gv_t){ (float)((x) - g->cam_x), (float)((y) - g->cam_y), 0, \
                            (float)((c) >> 16 & 0xFF), (float)((c) >> 8 & 0xFF), (float)((c) & 0xFF) }
    raster_gouraud(g, NULL, GV(x0, y0, c0), GV(x1, y1, c1), GV(x2, y2, c2));
#undef GV
}

/* ---------------------------------------------------------------- 3D */

int r3d_init(r3d_t *r, g16_t *g)
{
    memset(r, 0, sizeof *r);
    r->g = g;
    r->zbuf = malloc((size_t)g->w * g->h * 2);
    if (!r->zbuf)
        return -1;
    r3d_camera(r, 0, 0, -5, 0, 0, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.25f);
    r3d_zclear(r);
    return 0;
}

void r3d_free(r3d_t *r)
{
    free(r->zbuf);
    r->zbuf = NULL;
}

void r3d_zclear(r3d_t *r)
{
    memset(r->zbuf, 0, (size_t)r->g->w * r->g->h * 2);
    r->tris_in = r->tris_drawn = r->pixels = 0;
}

void r3d_camera(r3d_t *r, float x, float y, float z, float yaw, float pitch, float fov_deg)
{
    r->cam_pos = (v3_t){ x, y, z };
    r->cam_yaw = yaw;
    r->cam_pitch = pitch;
    r->cam_roll = 0;
    if (fov_deg < 10) fov_deg = 10;
    if (fov_deg > 150) fov_deg = 150;
    r->focal = (r->g->w * 0.5f) / tanf(fov_deg * 3.14159265f / 360.0f);
}

void r3d_camera_roll(r3d_t *r, float roll)
{
    r->cam_roll = roll;
}

void r3d_fog(r3d_t *r, uint32_t rgb, float near, float far)
{
    r->fog_rgb = rgb;
    r->fog_near = near;
    r->fog_far = far;
}

void r3d_lamp(r3d_t *r, int i, float x, float y, float z, float radius, float k)
{
    if (i < 0 || i >= R3D_LAMPS)
        return;
    r->lamp[i].pos = (v3_t){ x, y, z };
    r->lamp[i].r2 = radius * radius;
    r->lamp[i].k = k;
    r->lamp[i].on = radius > 0;
}

void r3d_light(r3d_t *r, float x, float y, float z, float ambient)
{
    float l = sqrtf(x * x + y * y + z * z);
    if (l < 1e-6f) l = 1;
    r->light = (v3_t){ x / l, y / l, z / l };
    r->ambient = ambient < 0 ? 0 : ambient > 1 ? 1 : ambient;
}

/* rotation matrix R = Rz * Ry * Rx, row major */
static void rot_matrix(float m[9], float rx, float ry, float rz)
{
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    m[0] = cz * cy; m[1] = cz * sy * sx - sz * cx; m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy; m[4] = sz * sy * sx + cz * cx; m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;     m[7] = cy * sx;                m[8] = cy * cx;
}

/* light factor k, then fog: fraction f of the fog colour */
static uint16_t shade(uint32_t rgb, float k, uint32_t fog, float f)
{
    float r = (rgb >> 16 & 0xFF) * k, g = (rgb >> 8 & 0xFF) * k, b = (rgb & 0xFF) * k;
    if (f > 0) {
        r += ((fog >> 16 & 0xFF) - r) * f;
        g += ((fog >> 8 & 0xFF) - g) * f;
        b += ((fog & 0xFF) - b) * f;
    }
    uint32_t ri = (uint32_t)r, gi = (uint32_t)g, bi = (uint32_t)b;
    return g16_rgb(ri > 255 ? 255 : ri, gi > 255 ? 255 : gi, bi > 255 ? 255 : bi);
}

typedef struct {
    float c[9];                 /* world -> camera rotation (yaw, pitch, roll) */
    float hw, hh, f;
} view_t;

static void view_setup(const r3d_t *r, view_t *v)
{
    const float cy = cosf(r->cam_yaw), sy = sinf(r->cam_yaw);
    const float cp = cosf(r->cam_pitch), sp = sinf(r->cam_pitch);
    const float cr = cosf(r->cam_roll), sr = sinf(r->cam_roll);
    /* x1 = cy x - sy z; z1 = sy x + cy z; y2 = cp y - sp z1; z2 = sp y + cp z1;
     * then the roll turns (x1, y2) in the screen plane */
    float ax = cy, az = -sy;                        /* x1 */
    float by = cp, bx = -sp * sy, bz = -sp * cy;    /* y2 */
    float zy = sp, zx = cp * sy, zz = cp * cy;      /* z2 */
    v->c[0] = cr * ax + sr * bx; v->c[1] = sr * by; v->c[2] = cr * az + sr * bz;
    v->c[3] = -sr * ax + cr * bx; v->c[4] = cr * by; v->c[5] = -sr * az + cr * bz;
    v->c[6] = zx; v->c[7] = zy; v->c[8] = zz;
    v->hw = r->g->w * 0.5f;
    v->hh = r->g->h * 0.5f;
    v->f = r->focal;
}

int r3d_project(const r3d_t *r, v3_t p, float *sx, float *sy, float *depth)
{
    view_t v;
    view_setup(r, &v);
    float wx = p.x - r->cam_pos.x, wy = p.y - r->cam_pos.y, wz = p.z - r->cam_pos.z;
    float x = v.c[0] * wx + v.c[1] * wy + v.c[2] * wz;
    float y = v.c[3] * wx + v.c[4] * wy + v.c[5] * wz;
    float z = v.c[6] * wx + v.c[7] * wy + v.c[8] * wz;
    *depth = z;
    if (z < NEAR)
        return 0;
    *sx = v.hw + x * v.f / z;
    *sy = v.hh - y * v.f / z;
    return 1;
}

/* camera space, texture coordinates, and the Gouraud colour (r, g, b; for
 * textured faces r is the light) */
typedef struct { float x, y, z, u, v, r, g, b; } cv_t;

static sv_t project(const view_t *v, cv_t c)
{
    float iz = 1.0f / c.z;
    return (sv_t){ v->hw + c.x * v->f * iz, v->hh - c.y * v->f * iz, iz };
}

/* The part of triangle abc in front of the near plane, as a fan of up to
 * 4 points. */
static int clip_near(const cv_t in[3], cv_t out[4])
{
    int n = 0;
    for (int i = 0; i < 3; i++) {
        cv_t a = in[i], b = in[(i + 1) % 3];
        int ia = a.z >= NEAR, ib = b.z >= NEAR;
        if (ia)
            out[n++] = a;
        if (ia != ib) {
            float t = (NEAR - a.z) / (b.z - a.z);
            out[n++] = (cv_t){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NEAR,
                               a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t,
                               a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
        }
    }
    return n;
}

#define MAX_VERTS 4096

void r3d_draw(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz, float scale)
{
    r3d_draw_flags(r, m, p, rx, ry, rz, scale, 0);
}

typedef struct {
    v3_t pos[R3D_LAMPS];        /* camera space */
    float r2[R3D_LAMPS], k[R3D_LAMPS];
    int n;
} lamps_t;

/* Light factor for a world-space normal at a camera-space point. */
static float light_k(const r3d_t *r, const lamps_t *L, float nx, float ny, float nz,
                     float px, float py, float pz)
{
    float d = nx * r->light.x + ny * r->light.y + nz * r->light.z;
    float k = r->ambient + (1.0f - r->ambient) * (d > 0 ? d : 0);
    for (int i = 0; i < L->n; i++) {
        float dx = px - L->pos[i].x, dy = py - L->pos[i].y, dz = pz - L->pos[i].z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < L->r2[i])
            k += L->k[i] * (1.0f - d2 / L->r2[i]);
    }
    return k;
}

/* Gouraud colour of a vertex: face colour times light, then fog. */
static void vertex_rgb(cv_t *c, uint32_t rgb, float k, uint32_t fog, float f)
{
    float r = (rgb >> 16 & 0xFF) * k, g = (rgb >> 8 & 0xFF) * k, b = (rgb & 0xFF) * k;
    if (f > 0) {
        r += ((fog >> 16 & 0xFF) - r) * f;
        g += ((fog >> 8 & 0xFF) - g) * f;
        b += ((fog & 0xFF) - b) * f;
    }
    c->r = r > 255 ? 255 : r;
    c->g = g > 255 ? 255 : g;
    c->b = b > 255 ? 255 : b;
}

void r3d_draw_flags(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz,
                    float scale, unsigned flags)
{
    uint16_t *zbuf = (flags & R3D_NOZ) ? NULL : r->zbuf;
    static sv_t sv[MAX_VERTS];
    static cv_t cv[MAX_VERTS];
    static float vk[MAX_VERTS];         /* Gouraud: light at each vertex */
    if (m->nverts > MAX_VERTS)
        return;

    float R[9];
    rot_matrix(R, rx, ry, rz);
    view_t v;
    view_setup(r, &v);
    const float *C = v.c;
    const int unlit = (flags & R3D_UNLIT) != 0;
    const int smooth = (flags & R3D_SMOOTH) && m->vnormals;
    const int fog = r->fog_far > r->fog_near;
    const float fog_k = fog ? 1.0f / (r->fog_far - r->fog_near) : 0;
    /* the lamps in camera space (distances do not change) */
    lamps_t lamps = { .n = 0 };
    for (int i = 0; i < R3D_LAMPS && !unlit; i++) {
        if (!r->lamp[i].on)
            continue;
        float wx = r->lamp[i].pos.x - r->cam_pos.x, wy = r->lamp[i].pos.y - r->cam_pos.y,
              wz = r->lamp[i].pos.z - r->cam_pos.z;
        lamps.pos[lamps.n] = (v3_t){ C[0] * wx + C[1] * wy + C[2] * wz, C[3] * wx + C[4] * wy + C[5] * wz,
                                     C[6] * wx + C[7] * wy + C[8] * wz };
        lamps.r2[lamps.n] = r->lamp[i].r2;
        lamps.k[lamps.n++] = r->lamp[i].k;
    }

    for (int i = 0; i < m->nverts; i++) {
        v3_t o = m->verts[i];
        /* object -> world, relative to the camera */
        float wx = (R[0] * o.x + R[1] * o.y + R[2] * o.z) * scale + p.x - r->cam_pos.x;
        float wy = (R[3] * o.x + R[4] * o.y + R[5] * o.z) * scale + p.y - r->cam_pos.y;
        float wz = (R[6] * o.x + R[7] * o.y + R[8] * o.z) * scale + p.z - r->cam_pos.z;
        cv_t c = { C[0] * wx + C[1] * wy + C[2] * wz, C[3] * wx + C[4] * wy + C[5] * wz,
                   C[6] * wx + C[7] * wy + C[8] * wz, 0, 0, 0, 0, 0 };
        cv[i] = c;
        if (c.z >= NEAR)
            sv[i] = project(&v, c);
        if (smooth) {
            v3_t n = m->vnormals[i];
            vk[i] = unlit ? 1.0f : light_k(r, &lamps, R[0] * n.x + R[1] * n.y + R[2] * n.z,
                                           R[3] * n.x + R[4] * n.y + R[5] * n.z,
                                           R[6] * n.x + R[7] * n.y + R[8] * n.z, c.x, c.y, c.z);
        }
    }

    for (int t = 0; t < m->nfaces; t++) {
        const uint16_t *fc = m->faces + t * 3;
        r->tris_in++;
        cv_t tri[3] = { cv[fc[0]], cv[fc[1]], cv[fc[2]] };
        int nin = (tri[0].z >= NEAR) + (tri[1].z >= NEAR) + (tri[2].z >= NEAR);
        if (nin == 0)
            continue;
        /* back faces first, on the vertices in front of the camera (the
         * clipped polygon has the same winding) */
        if (nin == 3) {
            sv_t a = sv[fc[0]], b = sv[fc[1]], c = sv[fc[2]];
            if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0)
                continue;                   /* y points down on screen */
        }
        const uint32_t rgb = m->colors[t];
        const int textured = (rgb & R3D_TEXTURED) && m->uv && m->tex;
        if (textured)
            for (int i = 0; i < 3; i++) {
                tri[i].u = m->uv[t * 6 + i * 2];
                tri[i].v = m->uv[t * 6 + i * 2 + 1];
            }

        if (smooth) {
            for (int i = 0; i < 3; i++) {
                float k = vk[fc[i]];
                if (textured) {
                    tri[i].r = k > 1 ? 1 : k;
                } else {
                    float ff = 0;
                    if (fog) {
                        ff = (tri[i].z - r->fog_near) * fog_k;
                        ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
                    }
                    vertex_rgb(&tri[i], rgb, k, r->fog_rgb, ff);
                }
            }
        } else {
            /* flat: Lambert with the face normal rotated to world space */
            float k = 1.0f;
            if (!unlit) {
                v3_t n = m->normals[t];
                k = light_k(r, &lamps, R[0] * n.x + R[1] * n.y + R[2] * n.z,
                            R[3] * n.x + R[4] * n.y + R[5] * n.z, R[6] * n.x + R[7] * n.y + R[8] * n.z,
                            (tri[0].x + tri[1].x + tri[2].x) * (1.0f / 3.0f),
                            (tri[0].y + tri[1].y + tri[2].y) * (1.0f / 3.0f),
                            (tri[0].z + tri[1].z + tri[2].z) * (1.0f / 3.0f));
            }
            if (textured) {
                for (int i = 0; i < 3; i++)
                    tri[i].r = k > 1 ? 1 : k;
            } else {
                float ff = 0;
                if (fog) {
                    ff = ((tri[0].z + tri[1].z + tri[2].z) * (1.0f / 3.0f) - r->fog_near) * fog_k;
                    ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
                }
                uint16_t col = shade(rgb, k, r->fog_rgb, ff);
                sv_t pts[4];
                int np = 3;
                if (nin == 3) {
                    pts[0] = sv[fc[0]]; pts[1] = sv[fc[1]]; pts[2] = sv[fc[2]];
                } else {
                    cv_t cl[4];
                    np = clip_near(tri, cl);
                    for (int i = 0; i < np; i++)
                        pts[i] = project(&v, cl[i]);
                    float area = (pts[1].x - pts[0].x) * (pts[2].y - pts[0].y) -
                                 (pts[1].y - pts[0].y) * (pts[2].x - pts[0].x);
                    if (area <= 0)
                        continue;
                }
                r->pixels += raster(r->g, zbuf, pts[0], pts[1], pts[2], col);
                if (np == 4)
                    r->pixels += raster(r->g, zbuf, pts[0], pts[2], pts[3], col);
                r->tris_drawn++;
                continue;
            }
        }

        /* per-vertex attributes: textured (flat or smooth) or Gouraud */
        cv_t cl[4];
        sv_t pts[4];
        int np;
        if (nin == 3) {
            cl[0] = tri[0]; cl[1] = tri[1]; cl[2] = tri[2];
            pts[0] = sv[fc[0]]; pts[1] = sv[fc[1]]; pts[2] = sv[fc[2]];
            np = 3;
        } else {
            np = clip_near(tri, cl);
            for (int i = 0; i < np; i++)
                pts[i] = project(&v, cl[i]);
            float area = (pts[1].x - pts[0].x) * (pts[2].y - pts[0].y) -
                         (pts[1].y - pts[0].y) * (pts[2].x - pts[0].x);
            if (area <= 0)
                continue;
        }
        if (textured) {
            tv_t tv[4];
            for (int i = 0; i < np; i++)
                tv[i] = (tv_t){ pts[i].x, pts[i].y, pts[i].z, cl[i].u * pts[i].z, cl[i].v * pts[i].z, cl[i].r };
            r->pixels += raster_tex(r->g, zbuf, tv[0], tv[1], tv[2], m->tex);
            if (np == 4)
                r->pixels += raster_tex(r->g, zbuf, tv[0], tv[2], tv[3], m->tex);
        } else {
            gv_t gv[4];
            for (int i = 0; i < np; i++)
                gv[i] = (gv_t){ pts[i].x, pts[i].y, pts[i].z, cl[i].r, cl[i].g, cl[i].b };
            r->pixels += raster_gouraud(r->g, zbuf, gv[0], gv[1], gv[2]);
            if (np == 4)
                r->pixels += raster_gouraud(r->g, zbuf, gv[0], gv[2], gv[3]);
        }
        r->tris_drawn++;
    }
}

/* ---------------------------------------------------------------- meshes */

int r3d_mesh_alloc(r3d_mesh_t *m, int nverts, int nfaces)
{
    memset(m, 0, sizeof *m);
    m->verts = calloc((size_t)nverts, sizeof *m->verts);
    m->faces = calloc((size_t)nfaces * 3, sizeof *m->faces);
    m->colors = calloc((size_t)nfaces, sizeof *m->colors);
    m->normals = calloc((size_t)nfaces, sizeof *m->normals);
    m->vnormals = calloc((size_t)nverts, sizeof *m->vnormals);
    if (!m->verts || !m->faces || !m->colors || !m->normals || !m->vnormals) {
        r3d_mesh_free(m);
        return -1;
    }
    m->nverts = nverts;
    m->nfaces = nfaces;
    return 0;
}

int r3d_mesh_alloc_uv(r3d_mesh_t *m)
{
    free(m->uv);
    m->uv = calloc((size_t)m->nfaces * 6, sizeof *m->uv);
    return m->uv ? 0 : -1;
}

void r3d_mesh_free(r3d_mesh_t *m)
{
    free(m->verts); free(m->faces); free(m->colors); free(m->normals); free(m->vnormals); free(m->uv);
    memset(m, 0, sizeof *m);
}

void r3d_mesh_normals(r3d_mesh_t *m)
{
    for (int t = 0; t < m->nfaces; t++) {
        v3_t a = m->verts[m->faces[t * 3]], b = m->verts[m->faces[t * 3 + 1]], c = m->verts[m->faces[t * 3 + 2]];
        v3_t u = { b.x - a.x, b.y - a.y, b.z - a.z }, v = { c.x - a.x, c.y - a.y, c.z - a.z };
        /* outward normal for the winding used by the meshes (u x v) */
        v3_t n = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (l < 1e-12f) l = 1;
        m->normals[t] = (v3_t){ n.x / l, n.y / l, n.z / l };
    }
    /* vertex normals for Gouraud: the average of the faces that share the
     * vertex (weighted by their area: n before normalizing is 2x area) */
    if (!m->vnormals)
        return;
    memset(m->vnormals, 0, (size_t)m->nverts * sizeof *m->vnormals);
    for (int t = 0; t < m->nfaces; t++) {
        v3_t a = m->verts[m->faces[t * 3]], b = m->verts[m->faces[t * 3 + 1]], c = m->verts[m->faces[t * 3 + 2]];
        v3_t u = { b.x - a.x, b.y - a.y, b.z - a.z }, v = { c.x - a.x, c.y - a.y, c.z - a.z };
        v3_t n = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        for (int k = 0; k < 3; k++) {
            v3_t *vn = &m->vnormals[m->faces[t * 3 + k]];
            vn->x += n.x; vn->y += n.y; vn->z += n.z;
        }
    }
    for (int i = 0; i < m->nverts; i++) {
        v3_t *vn = &m->vnormals[i];
        float l = sqrtf(vn->x * vn->x + vn->y * vn->y + vn->z * vn->z);
        if (l < 1e-12f) l = 1;
        vn->x /= l; vn->y /= l; vn->z /= l;
    }
}

int r3d_mesh_sphere(r3d_mesh_t *m, int rings, int segs, uint32_t c1, uint32_t c2)
{
    if (rings < 2) rings = 2;
    if (segs < 3) segs = 3;
    int nv = (rings + 1) * segs, nf = rings * segs * 2;
    if (nv > MAX_VERTS || r3d_mesh_alloc(m, nv, nf) != 0)
        return -1;
    for (int r = 0; r <= rings; r++) {
        float th = 3.14159265f * r / rings;
        for (int s = 0; s < segs; s++) {
            float ph = 2 * 3.14159265f * s / segs;
            m->verts[r * segs + s] = (v3_t){ sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph) };
        }
    }
    int f = 0;
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segs; s++) {
            uint16_t a = (uint16_t)(r * segs + s), b = (uint16_t)(r * segs + (s + 1) % segs);
            uint16_t c = (uint16_t)(a + segs), d = (uint16_t)(b + segs);
            uint32_t col = ((r + s) & 1) ? c1 : c2;
            m->faces[f * 3] = a; m->faces[f * 3 + 1] = b; m->faces[f * 3 + 2] = c; m->colors[f++] = col;
            m->faces[f * 3] = b; m->faces[f * 3 + 1] = d; m->faces[f * 3 + 2] = c; m->colors[f++] = col;
        }
    r3d_mesh_normals(m);
    return 0;
}

int r3d_mesh_cube(r3d_mesh_t *m, uint32_t color)
{
    static const float v[8][3] = {
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1},
    };
    static const uint16_t f[12][3] = {
        {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7}, {0,1,5}, {0,5,4},
        {3,6,2}, {3,7,6}, {0,4,7}, {0,7,3}, {1,2,6}, {1,6,5},
    };
    if (r3d_mesh_alloc(m, 8, 12) != 0)
        return -1;
    for (int i = 0; i < 8; i++) m->verts[i] = (v3_t){ v[i][0], v[i][1], v[i][2] };
    for (int i = 0; i < 12; i++) {
        memcpy(m->faces + i * 3, f[i], sizeof f[i]);
        m->colors[i] = color;
    }
    r3d_mesh_normals(m);
    return 0;
}
