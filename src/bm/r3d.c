#include "r3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR R3D_NEAR

/* ---------------------------------------------------------------- raster */

typedef struct { float x, y, z; } sv_t;     /* screen x, y; z = 1/depth */

/*
 * Triangle setup shared by the rasterizers. Pixel centres are sampled at
 * (x + 0.5, y + 0.5): a pixel is drawn when its centre is in [left edge,
 * right edge) and its row centre in [top, bottom), so shared edges are
 * drawn once. The vertices are sorted by y; the edges are walked in 32.32
 * fixed point (one 64-bit addition per row, no floating point compare),
 * and every attribute (depth, colour, texture) is linear in screen space,
 * with constant gradients d/dx and d/dy: no division per row.
 */
typedef struct {
    float ax, ay, bx, by;       /* top and middle vertex */
    float s_ac, s_ab, s_bc;     /* dx/dy of the long edge and the two short ones */
    float area;                 /* twice the signed area (for the gradients) */
    float inv_area;
    float e1x, e1y, e2x, e2y;   /* b - a, c - a */
    int y0, ymid, y1;           /* rows [y0, ymid) above b, [ymid, y1) below; clipped */
    int long_left;              /* the long edge a-c is on the left */
} tri_t;

/* ceil() for the pixel rules; exact for the values a screen can hold */
static inline int iceil(float f)
{
    int i = (int)f;
    return i + (f > (float)i);
}

static int tri_setup(tri_t *t, const g16_t *g, float ax, float ay, float bx, float by,
                     float cx, float cy)
{
    if (cy - ay < 1e-6f)
        return 0;
    t->ax = ax; t->ay = ay; t->bx = bx; t->by = by;
    t->e1x = bx - ax; t->e1y = by - ay; t->e2x = cx - ax; t->e2y = cy - ay;
    t->area = t->e1x * t->e2y - t->e2x * t->e1y;
    if (t->area > -1e-6f && t->area < 1e-6f)
        return 0;
    t->inv_area = 1.0f / t->area;
    t->long_left = t->area > 0;
    t->s_ac = (cx - ax) / (cy - ay);
    t->s_ab = by - ay > 1e-6f ? (bx - ax) / (by - ay) : 0.0f;
    t->s_bc = cy - by > 1e-6f ? (cx - bx) / (cy - by) : 0.0f;
    t->y0 = iceil(ay - 0.5f);
    t->ymid = iceil(by - 0.5f);
    t->y1 = iceil(cy - 0.5f);
    if (t->y0 < g->cy0) t->y0 = g->cy0;
    if (t->y1 > g->cy1) t->y1 = g->cy1;
    if (t->ymid < t->y0) t->ymid = t->y0;
    if (t->ymid > t->y1) t->ymid = t->y1;
    return t->y0 < t->y1;
}

/* Gradients of an attribute with values va, vb, vc at the vertices. */
static inline void tri_grad(const tri_t *t, float va, float vb, float vc, float *ddx, float *ddy)
{
    float d1 = vb - va, d2 = vc - va, inv = t->inv_area;
    *ddx = (d1 * t->e2y - d2 * t->e1y) * inv;
    *ddy = (d2 * t->e1x - d1 * t->e2x) * inv;
}

/* f in 32.32 fixed point (limited to +-2^30) */
static inline int64_t fx32(float f)
{
    double d = (double)f;
    if (d > 1073741824.0) d = 1073741824.0;
    if (d < -1073741824.0) d = -1073741824.0;
    d += 2147483648.0;                  /* positive: the conversion is a floor */
    uint32_t hi = (uint32_t)d;
    uint32_t lo = (uint32_t)((d - (double)hi) * 4294967296.0);
    return (int64_t)(((uint64_t)(hi - 0x80000000u) << 32) | lo);
}

/* first pixel whose centre is at or right of x (32.32): ceil(x - 0.5) */
#define PIX(X) ((int)(((X) + 0x7FFFFFFFLL) >> 32))

/*
 * The rows of a set-up triangle: the body runs for every row y with pixels
 * [x0, x1) (clipped, not empty). The left and right edges change at the
 * middle vertex; the long edge goes on.
 */
#define SCAN(t, g, ...) do {                                                                 \
    const tri_t *T_ = (t);                                                                 \
    int64_t lng_ = fx32(T_->ax + ((float)T_->y0 + 0.5f - T_->ay) * T_->s_ac), slng_ = fx32(T_->s_ac); \
    int64_t sht_ = 0, ssht_ = 0;                                                           \
    if (T_->y0 < T_->ymid) {                                                               \
        sht_ = fx32(T_->ax + ((float)T_->y0 + 0.5f - T_->ay) * T_->s_ab);                    \
        ssht_ = fx32(T_->s_ab);                                                            \
    }                                                                                      \
    int y = T_->y0;                                                                        \
    for (int half_ = 0; half_ < 2; half_++) {                                              \
        const int yend_ = half_ ? T_->y1 : T_->ymid;                                       \
        if (y >= yend_)                                                                    \
            continue;                                                                      \
        if (half_) {                                                                       \
            sht_ = fx32(T_->bx + ((float)y + 0.5f - T_->by) * T_->s_bc);                     \
            ssht_ = fx32(T_->s_bc);                                                        \
        }                                                                                  \
        int64_t l_ = T_->long_left ? lng_ : sht_, sl_ = T_->long_left ? slng_ : ssht_;     \
        int64_t r_ = T_->long_left ? sht_ : lng_, sr_ = T_->long_left ? ssht_ : slng_;     \
        for (; y < yend_; y++, l_ += sl_, r_ += sr_) {                                     \
            int x0 = PIX(l_), x1 = PIX(r_);                                                \
            if (x0 < (g)->cx0) x0 = (g)->cx0;                                              \
            if (x1 > (g)->cx1) x1 = (g)->cx1;                                              \
            if (x0 >= x1)                                                                  \
                continue;                                                                  \
            __VA_ARGS__                                                                    \
        }                                                                                  \
        lng_ = T_->long_left ? l_ : r_;                                                    \
    }                                                                                      \
} while (0)

/* value of an attribute at the centre of pixel (x, y) */
#define ATTR(t, va, ddx, ddy, x, y) ((va) + ((x) + 0.5f - (t)->ax) * (ddx) + ((y) + 0.5f - (t)->ay) * (ddy))

#define SORT3(T, a, b, c) do { T s_;                            \
        if (a.y > b.y) { s_ = a; a = b; b = s_; }               \
        if (b.y > c.y) { s_ = b; b = c; c = s_; }               \
        if (a.y > b.y) { s_ = a; a = b; b = s_; } } while (0)

/* z = 1/depth, 16-bit z-buffer: bigger = nearer; stepped in 24.8 */
#define ZSCALE (65535.0f * 256.0f)

/* (zf >> 8) limited to 0..65535 (a negative depth never passes the test) */
static inline uint32_t zsat(int32_t zf)
{
#if defined(__arm__)
    uint32_t r;
    __asm__("usat %0, #16, %1, asr #8" : "=r"(r) : "r"(zf));
    return r;
#else
    int32_t z = zf >> 8;
    return z < 0 ? 0 : z > 65535 ? 65535 : (uint32_t)z;
#endif
}

typedef uint32_t __attribute__((may_alias)) u32a_t;

/* n pixels of colour c, two at a time */
static inline void span_fill(uint16_t *p, int n, uint16_t c)
{
    if (((uintptr_t)p & 2) && n > 0) {
        *p++ = c;
        n--;
    }
    const uint32_t c2 = c | (uint32_t)c << 16;
    u32a_t *q = (u32a_t *)p;
    for (; n >= 8; n -= 8, q += 4) {
        q[0] = c2; q[1] = c2; q[2] = c2; q[3] = c2;
    }
    for (; n >= 2; n -= 2)
        *q++ = c2;
    if (n)
        *(uint16_t *)q = c;
}

/*
 * Flat triangle. With zbuf == NULL it is a plain 2D fill.
 */
static uint32_t raster(g16_t *g, uint16_t *zbuf, sv_t a, sv_t b, sv_t c, uint16_t color)
{
    SORT3(sv_t, a, b, c);
    tri_t t;
    if (!tri_setup(&t, g, a.x, a.y, b.x, b.y, c.x, c.y))
        return 0;
    float dzx = 0, dzy = 0;
    if (zbuf)
        tri_grad(&t, a.z, b.z, c.z, &dzx, &dzy);
    const int32_t dzf = (int32_t)(dzx * ZSCALE);
    uint32_t count = 0;

    SCAN(&t, g, {
        uint16_t *p = g->px + (uint32_t)y * g->stride + x0;
        int n = x1 - x0;
        if (!zbuf) {
            span_fill(p, n, color);
            count += (uint32_t)n;
            continue;
        }
        int32_t zf = (int32_t)(ATTR(&t, a.z, dzx, dzy, x0, y) * ZSCALE);
        uint16_t *zp = zbuf + (uint32_t)y * g->w + x0;
        do {
            uint32_t zz = zsat(zf);
            zf += dzf;
            if (zz > *zp) {
                *zp = (uint16_t)zz;
                *p = color;
                count++;
            }
            zp++;
            p++;
        } while (--n);
    });
    return count;
}

/* 4x4 ordered dither, 0..15 ({ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1,
 * 9 }, { 15, 7, 13, 5 }): the colour steps of RGB565 (8 levels of red and
 * blue, 4 of green) become a fine pattern instead of bands. Per pixel the
 * offsets are (d >> 1, d >> 2, d >> 1). */

/* Gouraud triangle: the colour (r, g, b in 0..255) is interpolated across
 * the face, in screen space, and dithered. With zbuf == NULL, a 2D fill. */
typedef struct { float x, y, z, r, g, b; } gv_t;

static inline float climit(float v, float hi)
{
    return v < 0.5f ? 0.5f : v > hi ? hi : v;
}

/* the dither offsets of the four columns of a row of bayer4, one byte per
 * column: d >> 1 (red and blue) in bits 0-2, d >> 2 (green) in bits 3-4 */
#define DBYTE(d) ((uint32_t)((d) >> 1 | ((d) >> 2) << 3))
#define DROW(a, b, c, d) (DBYTE(a) | DBYTE(b) << 8 | DBYTE(c) << 16 | DBYTE(d) << 24)
static const uint32_t dither_row[4] = {
    DROW(0, 8, 2, 10), DROW(12, 4, 14, 6), DROW(3, 11, 1, 9), DROW(15, 7, 13, 5),
};

static inline uint32_t ror8(uint32_t v, unsigned n)
{
    return n ? v >> n | v << (32 - n) : v;
}

/* A dithered RGB565 pixel from 16.16 colours whose integer parts are at
 * most 248 / 252 / 248 (the vertex colours are limited so that the dither
 * never overflows: 248 + 7 still rounds to the top level); d is the
 * dither byte of the pixel's column (low byte of the rotating pattern). */
static inline uint16_t dither565(uint32_t cr, uint32_t cg, uint32_t cb, uint32_t d)
{
    const uint32_t rb = (d & 7) << 16, gd = (d & 0x18) << 13;
    return (uint16_t)(((cr + rb) >> 8 & 0xF800) | ((cg + gd) >> 13 & 0x07E0) | (cb + rb) >> 19);
}

static uint32_t raster_gouraud(g16_t *g, uint16_t *zbuf, gv_t a, gv_t b, gv_t c)
{
    SORT3(gv_t, a, b, c);
    tri_t t;
    if (!tri_setup(&t, g, a.x, a.y, b.x, b.y, c.x, c.y))
        return 0;
    a.r = climit(a.r, 248.4f); b.r = climit(b.r, 248.4f); c.r = climit(c.r, 248.4f);
    a.g = climit(a.g, 252.4f); b.g = climit(b.g, 252.4f); c.g = climit(c.g, 252.4f);
    a.b = climit(a.b, 248.4f); b.b = climit(b.b, 248.4f); c.b = climit(c.b, 248.4f);
    float drx, dry, dgx, dgy, dbx, dby, dzx = 0, dzy = 0;
    tri_grad(&t, a.r, b.r, c.r, &drx, &dry);
    tri_grad(&t, a.g, b.g, c.g, &dgx, &dgy);
    tri_grad(&t, a.b, b.b, c.b, &dbx, &dby);
    if (zbuf)
        tri_grad(&t, a.z, b.z, c.z, &dzx, &dzy);
    /* 16.16 fixed point across the spans */
    const int32_t ir = (int32_t)(drx * 65536.0f), ig = (int32_t)(dgx * 65536.0f),
                  ib = (int32_t)(dbx * 65536.0f), dzf = (int32_t)(dzx * ZSCALE);
    uint32_t count = 0;

    SCAN(&t, g, {
        /* the edge rounding can step a hair outside the vertex range */
        uint32_t cr = (uint32_t)(int32_t)(climit(ATTR(&t, a.r, drx, dry, x0, y), 248.4f) * 65536.0f);
        uint32_t cg = (uint32_t)(int32_t)(climit(ATTR(&t, a.g, dgx, dgy, x0, y), 252.4f) * 65536.0f);
        uint32_t cb = (uint32_t)(int32_t)(climit(ATTR(&t, a.b, dbx, dby, x0, y), 248.4f) * 65536.0f);
        /* the dither bytes from column x0 on, one per pixel */
        uint32_t pat = ror8(dither_row[y & 3], 8u * (unsigned)(x0 & 3));
        uint16_t *p = g->px + (uint32_t)y * g->stride + x0;
        int n = x1 - x0;
        if (!zbuf) {
            count += (uint32_t)n;
            do {
                *p++ = dither565(cr, cg, cb, pat);
                pat = ror8(pat, 8);
                cr += (uint32_t)ir; cg += (uint32_t)ig; cb += (uint32_t)ib;
            } while (--n);
            continue;
        }
        int32_t zf = (int32_t)(ATTR(&t, a.z, dzx, dzy, x0, y) * ZSCALE);
        uint16_t *zp = zbuf + (uint32_t)y * g->w + x0;
        do {
            uint32_t zz = zsat(zf);
            zf += dzf;
            if (zz > *zp) {
                *zp = (uint16_t)zz;
                *p = dither565(cr, cg, cb, pat);
                count++;
            }
            pat = ror8(pat, 8);
            cr += (uint32_t)ir; cg += (uint32_t)ig; cb += (uint32_t)ib;
            p++;
            zp++;
        } while (--n);
    });
    return count;
}

/* Textured triangle: u and v are divided by depth at the vertices and
 * interpolated with 1/z, then divided back every TEX_RUN pixels and linear in
 * between (perspective correct to a fraction of a texel, one division per run).
 * k = light at each vertex, 0..1 (the same at the three vertices for flat
 * shading). Transparent texels are skipped. */
#define TEX_RUN 16                  /* pixels between exact perspective divisions */

typedef struct { float x, y, z, u, v, k; } tv_t;    /* z = 1/depth; u, v premultiplied by z */

/* texel p times the light k (0..256), as ((c * k) >> 8) for each of R, G
 * and B: red and blue share one multiplication */
static inline uint32_t mod565(uint32_t p, uint32_t k)
{
    uint32_t rb = (((p & 0xF800) << 5) | (p & 0x1F)) * k;
    uint32_t gg = ((p & 0x07E0) * k) >> 8 & 0x07E0;
    return (rb >> 13 & 0xF800) | (rb >> 8 & 0x1F) | gg;
}

/* the light of a texel: none (k >= 1 everywhere), the same for the
 * whole face, or interpolated */
enum { TL_NONE, TL_FLAT, TL_SMOOTH };

/*
 * n pixels of a texture run whose texels are all inside the texture (no
 * clamp). ZT: depth test; ALPHA: skip transparent texels; LIGHT: TL_*.
 * The constant arguments are folded where the function is inlined.
 */
static inline __attribute__((always_inline))
uint32_t tex_run(uint16_t *p, uint16_t *zp, int n, int32_t zf, int32_t dzf, int32_t uf, int32_t duf,
                 int32_t vf, int32_t dvf, int32_t kf, int32_t dkf, const uint16_t *px,
                 const uint8_t *alpha, uint32_t tw, const int ZT, const int ALPHA, const int LIGHT)
{
    uint32_t count = 0;
    const uint32_t kc = LIGHT == TL_FLAT ? (uint32_t)(kf <= 0 ? 0 : kf >> 8) : 0;
    do {
        uint32_t zz = 0;
        if (ZT) {
            zz = zsat(zf);
            zf += dzf;
        }
        if (!ZT || zz > *zp) {
            uint32_t i = (uint32_t)(vf >> 16) * tw + (uint32_t)(uf >> 16);
            if (!ALPHA || alpha[i]) {
                uint32_t c = px[i];
                if (LIGHT == TL_FLAT) {
                    c = mod565(c, kc);
                } else if (LIGHT == TL_SMOOTH) {
                    int32_t k = kf >> 8;
                    c = mod565(c, k <= 0 ? 0 : k > 256 ? 256 : (uint32_t)k);
                }
                if (ZT)
                    *zp = (uint16_t)zz;
                *p = (uint16_t)c;
                count++;
            }
        }
        uf += duf;
        vf += dvf;
        if (LIGHT == TL_SMOOTH)
            kf += dkf;
        p++;
        if (ZT)
            zp++;
    } while (--n);
    return count;
}

/* the same with clamped texel coordinates, for the runs that reach out of
 * the texture (the general case, as before M30) */
static uint32_t tex_run_clamp(uint16_t *p, uint16_t *zp, int n, int32_t zf, int32_t dzf, int32_t uf,
                              int32_t duf, int32_t vf, int32_t dvf, int32_t kf, int32_t dkf,
                              const g16_sheet_t *tex)
{
    const int tw = tex->w, th = tex->h;
    uint32_t count = 0;
    for (; n > 0; n--, p++, zf += dzf, uf += duf, vf += dvf, kf += dkf) {
        uint32_t zz = zsat(zf);
        if (zp) {
            if (zz <= *zp) {
                zp++;
                continue;
            }
        }
        int tx = uf >> 16, ty = vf >> 16;
        if ((unsigned)tx >= (unsigned)tw) tx = tx < 0 ? 0 : tw - 1;
        if ((unsigned)ty >= (unsigned)th) ty = ty < 0 ? 0 : th - 1;
        uint32_t i = (uint32_t)ty * (uint32_t)tw + (uint32_t)tx;
        if (tex->alpha[i]) {
            int32_t k = kf >> 8;
            if (zp)
                *zp = (uint16_t)zz;
            *p = (uint16_t)mod565(tex->px[i], k <= 0 ? 0 : k > 256 ? 256 : (uint32_t)k);
            count++;
        }
        if (zp)
            zp++;
    }
    return count;
}

/* Whether every texel between two texel coordinates (16.16, inside the
 * sheet) is opaque: the 8x8 cells of the sheet are flagged when all their
 * pixels are. Long runs across many cells are not checked. */
static inline int run_opaque(const g16_sheet_t *s, int32_t ua, int32_t ub, int32_t va, int32_t vb)
{
    int cx0 = ua >> 19, cx1 = ub >> 19, cy0 = va >> 19, cy1 = vb >> 19;
    if (cx0 > cx1) { int t = cx0; cx0 = cx1; cx1 = t; }
    if (cy0 > cy1) { int t = cy0; cy0 = cy1; cy1 = t; }
    if (cx1 - cx0 > 3 || cy1 - cy0 > 3)
        return 0;
    const int cw = s->w / G16_CELL;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
            if (!s->cell_opaque[cy * cw + cx])
                return 0;
    return 1;
}

/* gradients of a textured face along x, and its light */
typedef struct {
    float dzx, dux, dvx;
    int32_t dzf, dkf;
    int light;                  /* TL_* */
} texgrad_t;

#define TEX_RUN_CASE(ZT, ALPHA, LIGHT) \
    tex_run(p, zp, n, zf, gr->dzf, uf, duf, vf, dvf, kf, gr->dkf, px, alpha, tw, ZT, ALPHA, LIGHT)

/* 1.0f / n for the short runs (the same values as the division) */
static const float recip[TEX_RUN] = {
    0, 1.0f / 1, 1.0f / 2, 1.0f / 3, 1.0f / 4, 1.0f / 5, 1.0f / 6, 1.0f / 7, 1.0f / 8,
    1.0f / 9, 1.0f / 10, 1.0f / 11, 1.0f / 12, 1.0f / 13, 1.0f / 14, 1.0f / 15,
};

/* One row of a textured face: pixels [0, len) from p (and zp, or NULL
 * without the z-buffer); z, u, v and kf at the first pixel. */
static uint32_t tex_span(uint16_t *p, uint16_t *zp, int len, float z, float u, float v, int32_t kf,
                         const texgrad_t *gr, const g16_sheet_t *tex)
{
    const uint32_t tw = (uint32_t)tex->w, th = (uint32_t)tex->h;
    const uint16_t *px = tex->px;
    const uint8_t *alpha = tex->alpha;
    const int light = gr->light;
    uint32_t count = 0;
    int32_t zf = (int32_t)(z * ZSCALE);
    /* texel coordinates (16.16) exact every TEX_RUN pixels, linear in between */
    float iz = 1.0f / (z > 1e-9f ? z : 1e-9f);
    int32_t uf = (int32_t)(u * iz * 65536.0f), vf = (int32_t)(v * iz * 65536.0f);
    for (int x = 0; x < len;) {
        int n = len - x < TEX_RUN ? len - x : TEX_RUN;
        z += gr->dzx * n; u += gr->dux * n; v += gr->dvx * n;
        float iz2 = 1.0f / (z > 1e-9f ? z : 1e-9f);
        int32_t uf2 = (int32_t)(u * iz2 * 65536.0f), vf2 = (int32_t)(v * iz2 * 65536.0f);
        int32_t duf, dvf;
        if (n == TEX_RUN) {
            duf = (uf2 - uf) / TEX_RUN; dvf = (vf2 - vf) / TEX_RUN;
        } else {
            const float in = recip[n];
            duf = (int32_t)((float)(uf2 - uf) * in); dvf = (int32_t)((float)(vf2 - vf) * in);
        }
        /* u and v are linear along the run: if its first and last texels
         * are inside the texture, all of them are */
        const int32_t ue = uf + (n - 1) * duf, ve = vf + (n - 1) * dvf;
        if ((uint32_t)(uf >> 16) < tw && (uint32_t)(ue >> 16) < tw &&
            (uint32_t)(vf >> 16) < th && (uint32_t)(ve >> 16) < th) {
            const int opaque = run_opaque(tex, uf, ue, vf, ve);
            if (zp) {
                if (opaque) {
                    if (light == TL_NONE) count += TEX_RUN_CASE(1, 0, TL_NONE);
                    else if (light == TL_FLAT) count += TEX_RUN_CASE(1, 0, TL_FLAT);
                    else count += TEX_RUN_CASE(1, 0, TL_SMOOTH);
                } else {
                    if (light == TL_NONE) count += TEX_RUN_CASE(1, 1, TL_NONE);
                    else if (light == TL_FLAT) count += TEX_RUN_CASE(1, 1, TL_FLAT);
                    else count += TEX_RUN_CASE(1, 1, TL_SMOOTH);
                }
            } else {
                if (opaque) {
                    if (light == TL_NONE) count += TEX_RUN_CASE(0, 0, TL_NONE);
                    else if (light == TL_FLAT) count += TEX_RUN_CASE(0, 0, TL_FLAT);
                    else count += TEX_RUN_CASE(0, 0, TL_SMOOTH);
                } else {
                    if (light == TL_NONE) count += TEX_RUN_CASE(0, 1, TL_NONE);
                    else if (light == TL_FLAT) count += TEX_RUN_CASE(0, 1, TL_FLAT);
                    else count += TEX_RUN_CASE(0, 1, TL_SMOOTH);
                }
            }
        } else {
            count += tex_run_clamp(p, zp, n, zf, gr->dzf, uf, duf, vf, dvf, kf, gr->dkf, tex);
        }
        x += n;
        p += n;
        if (zp)
            zp += n;
        zf += gr->dzf * n;
        kf += gr->dkf * n;
        uf = uf2; vf = vf2;
    }
    return count;
}

#undef TEX_RUN_CASE

static uint32_t raster_tex(g16_t *g, uint16_t *zbuf, tv_t a, tv_t b, tv_t c,
                           const g16_sheet_t *tex)
{
    SORT3(tv_t, a, b, c);
    tri_t t;
    if (!tri_setup(&t, g, a.x, a.y, b.x, b.y, c.x, c.y))
        return 0;
    float dzx, dzy, dux, duy, dvx, dvy, dkx, dky;
    tri_grad(&t, a.z, b.z, c.z, &dzx, &dzy);
    tri_grad(&t, a.u, b.u, c.u, &dux, &duy);
    tri_grad(&t, a.v, b.v, c.v, &dvx, &dvy);
    tri_grad(&t, a.k, b.k, c.k, &dkx, &dky);
    const int flat_k = a.k == b.k && b.k == c.k;
    /* light in 8.8 fixed point of 0..256 */
    texgrad_t gr = { dzx, dux, dvx, (int32_t)(dzx * ZSCALE), flat_k ? 0 : (int32_t)(dkx * 65536.0f),
                     flat_k ? ((int32_t)(a.k * 65536.0f) >> 8 >= 256 ? TL_NONE : TL_FLAT) : TL_SMOOTH };
    uint32_t count = 0;

    SCAN(&t, g, {
        float z = ATTR(&t, a.z, dzx, dzy, x0, y), u = ATTR(&t, a.u, dux, duy, x0, y),
              v = ATTR(&t, a.v, dvx, dvy, x0, y);
        int32_t kf = (int32_t)((flat_k ? a.k : ATTR(&t, a.k, dkx, dky, x0, y)) * 65536.0f);
        uint16_t *p = g->px + (uint32_t)y * g->stride + x0;
        uint16_t *zp = zbuf ? zbuf + (uint32_t)y * g->w + x0 : NULL;
        count += tex_span(p, zp, x1 - x0, z, u, v, kf, &gr, tex);
    });
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
    if (r->backend)
        r->backend->zclear(r->backend->ctx, r->g);
    else
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
    if (rx == 0 && ry == 0 && rz == 0) {        /* most scenery: no trigonometry */
        static const float id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        memcpy(m, id, sizeof id);
        return;
    }
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    m[0] = cz * cy; m[1] = cz * sy * sx - sz * cx; m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy; m[4] = sz * sy * sx + cz * cx; m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;     m[7] = cy * sx;                m[8] = cy * cx;
}

/* the colour of a flat face for a backend: r, g, b in 0..255 */
static void shade_rgb(uint32_t rgb, float k, uint32_t fog, float f, float out[3])
{
    float c[3] = { (rgb >> 16 & 0xFF) * k, (rgb >> 8 & 0xFF) * k, (rgb & 0xFF) * k };
    for (int i = 0; i < 3; i++) {
        if (f > 0)
            c[i] += ((fog >> (16 - 8 * i) & 0xFF) - c[i]) * f;
        out[i] = c[i] > 255 ? 255 : c[i];
    }
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
    float side, top;            /* |(f, hw)| and |(f, hh)|: the screen edges as planes */
} view_t;

static void view_setup(const r3d_t *r, view_t *v)
{
    /* the camera rarely changes within a frame: many draw3d() calls share
     * the same matrix (six sines and cosines saved each time) */
    static struct { float yaw, pitch, roll, focal; int w, h, ok; view_t v; } cache;
    if (cache.ok && cache.yaw == r->cam_yaw && cache.pitch == r->cam_pitch &&
        cache.roll == r->cam_roll && cache.focal == r->focal && cache.w == r->g->w && cache.h == r->g->h) {
        *v = cache.v;
        return;
    }
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
    v->side = sqrtf(v->f * v->f + v->hw * v->hw);
    v->top = sqrtf(v->f * v->f + v->hh * v->hh);
    cache.yaw = r->cam_yaw; cache.pitch = r->cam_pitch; cache.roll = r->cam_roll;
    cache.focal = r->focal; cache.w = r->g->w; cache.h = r->g->h;
    cache.v = *v;
    cache.ok = 1;
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

/* a triangle, or the two of a quad left by the near plane, to the backend */
static void emit(r3d_t *r, const r3d_corner_t *q, int np, int kind, const g16_sheet_t *tex, int nodepth)
{
    r->backend->tri(r->backend->ctx, r->g, q, kind, tex, nodepth);
    if (np == 4) {
        const r3d_corner_t q2[3] = { q[0], q[2], q[3] };
        r->backend->tri(r->backend->ctx, r->g, q2, kind, tex, nodepth);
    }
}

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

    view_t v;
    view_setup(r, &v);
    const float *C = v.c;
    if (m->radius >= 0) {
        /* the mesh's bounding sphere, in camera space, against the near
         * plane and the four planes through the eye and the screen edges */
        const float rad = m->radius * fabsf(scale);
        const float wx = p.x - r->cam_pos.x, wy = p.y - r->cam_pos.y, wz = p.z - r->cam_pos.z;
        const float cx = C[0] * wx + C[1] * wy + C[2] * wz, cy = C[3] * wx + C[4] * wy + C[5] * wz,
                    cz = C[6] * wx + C[7] * wy + C[8] * wz;
        if (cz + rad < NEAR || v.f * fabsf(cx) - v.hw * cz > rad * v.side ||
            v.f * fabsf(cy) - v.hh * cz > rad * v.top) {
            r->tris_in += (uint32_t)m->nfaces;
            return;
        }
    }
    float R[9];
    rot_matrix(R, rx, ry, rz);
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
        int nin = (cv[fc[0]].z >= NEAR) + (cv[fc[1]].z >= NEAR) + (cv[fc[2]].z >= NEAR);
        if (nin == 0)
            continue;
        /* back faces first, on the vertices in front of the camera (the
         * clipped polygon has the same winding) */
        if (nin == 3) {
            const sv_t *a = &sv[fc[0]], *b = &sv[fc[1]], *c = &sv[fc[2]];
            if ((b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x) <= 0)
                continue;                   /* y points down on screen */
        }
        cv_t tri[3] = { cv[fc[0]], cv[fc[1]], cv[fc[2]] };
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
                if (r->backend) {
                    float c[3];
                    shade_rgb(rgb, k, r->fog_rgb, ff, c);
                    r3d_corner_t q[4];
                    for (int i = 0; i < np; i++)
                        q[i] = (r3d_corner_t){ pts[i].x, pts[i].y, pts[i].z, c[0], c[1], c[2] };
                    emit(r, q, np, R3D_KIND_COLOUR, NULL, zbuf == NULL);
                    r->tris_drawn++;
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
        if (r->backend) {
            r3d_corner_t q[4];
            for (int i = 0; i < np; i++)
                q[i] = textured ? (r3d_corner_t){ pts[i].x, pts[i].y, pts[i].z, cl[i].u, cl[i].v, cl[i].r }
                                : (r3d_corner_t){ pts[i].x, pts[i].y, pts[i].z, cl[i].r, cl[i].g, cl[i].b };
            emit(r, q, np, textured ? R3D_KIND_TEXTURE : R3D_KIND_COLOUR, textured ? m->tex : NULL,
                 zbuf == NULL);
        } else if (textured) {
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
    m->radius = -1;
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
    float r2 = 0;
    for (int i = 0; i < m->nverts; i++) {
        v3_t v = m->verts[i];
        float d = v.x * v.x + v.y * v.y + v.z * v.z;
        if (d > r2) r2 = d;
    }
    m->radius = sqrtf(r2) * 1.0001f + 1e-6f;
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
