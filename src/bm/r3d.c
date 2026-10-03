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
    float area, inv;            /* twice the signed area (for the gradients), 1 / area */
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
    t->inv = 1.0f / t->area;
    t->long_left = t->area > 0;
    t->s_ac = (cx - ax) / (cy - ay);
    t->s_ab = by - ay > 1e-6f ? (bx - ax) / (by - ay) : 0.0f;
    t->s_bc = cy - by > 1e-6f ? (cx - bx) / (cy - by) : 0.0f;
    t->y0 = iceil(ay - 0.5f);
    t->ymid = iceil(by - 0.5f);
    t->y1 = iceil(cy - 0.5f);
    if (t->y0 < g->cy0) t->y0 = g->cy0;
    if (t->y1 > g->cy1) t->y1 = g->cy1;
    /* a triangle that reaches past the left or right of the clip (a wall
     * beside the camera, cut by the near plane, can be thousands of pixels
     * wide): only the rows where it is between them, not empty spans */
    const float X0 = (float)g->cx0, X1 = (float)g->cx1;
    if (t->y0 < t->y1 && (ax < X0 || bx < X0 || cx < X0 || ax > X1 || bx > X1 || cx > X1)) {
        const float px[3] = { ax, bx, cx }, py[3] = { ay, by, cy };
        float ylo = 1e30f, yhi = -1e30f;
        for (int i = 0; i < 3; i++) {
            if (px[i] >= X0 && px[i] <= X1) {
                ylo = py[i] < ylo ? py[i] : ylo;
                yhi = py[i] > yhi ? py[i] : yhi;
            }
            const int j = i == 2 ? 0 : i + 1;
            for (int k = 0; k < 2; k++) {
                const float d0 = px[i] - (k ? X1 : X0), d1 = px[j] - (k ? X1 : X0);
                if ((d0 < 0) != (d1 < 0)) {
                    const float yy = py[i] + (py[j] - py[i]) * (d0 / (d0 - d1));
                    ylo = yy < ylo ? yy : ylo;
                    yhi = yy > yhi ? yy : yhi;
                }
            }
        }
        if (ylo > yhi)
            return 0;
        const int a = iceil(ylo - 0.5f) - 1, b = iceil(yhi - 0.5f) + 1;     /* a row more each way */
        if (t->y0 < a) t->y0 = a;
        if (t->y1 > b) t->y1 = b;
    }
    if (t->ymid < t->y0) t->ymid = t->y0;
    if (t->ymid > t->y1) t->ymid = t->y1;
    return t->y0 < t->y1;
}

/* Gradients of an attribute with values va, vb, vc at the vertices. */
static inline void tri_grad(const tri_t *t, float va, float vb, float vc, float *ddx, float *ddy)
{
    float d1 = vb - va, d2 = vc - va;
    *ddx = (d1 * t->e2y - d2 * t->e1y) * t->inv;
    *ddy = (d2 * t->e1x - d1 * t->e2x) * t->inv;
}

/* the value of an attribute at pixel (0, 0): then at (x, y) it is
 * base + x * ddx + y * ddy (pixel centres) */
static inline float tri_base(const tri_t *t, float va, float ddx, float ddy)
{
    return va + (0.5f - t->ax) * ddx + (0.5f - t->ay) * ddy;
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

/* a saturating clamp of z (1/depth in 24.8) to the 16 bits of the z-buffer
 * (a negative depth never passes the test) */
static inline uint32_t zsat(int32_t zf)
{
#if defined(__ARM_ARCH) && __ARM_ARCH >= 6 && !defined(__thumb__)
    uint32_t r;
    __asm__("usat %0, #16, %1, asr #8" : "=r"(r) : "r"(zf));
    return r;
#else
    int32_t zz = zf >> 8;
    return zz < 0 ? 0 : zz > 65535 ? 65535 : (uint32_t)zz;
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
 * Flat triangle. With zbuf == NULL it is a plain 2D fill. With `screen`, only
 * the pixels with (x + y) even (screen-door transparency); the others keep
 * what is behind, and the depth there is not written.
 */
static uint32_t raster(g16_t *g, uint16_t *zbuf, sv_t a, sv_t b, sv_t c, uint16_t color, int screen)
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

    if (screen) {
        const int32_t dz = dzf * 2;
        SCAN(&t, g, {
            if (((x0 + y) & 1) && ++x0 >= x1)
                continue;
            uint16_t *row = g->px + (uint32_t)y * g->stride;
            if (!zbuf) {
                for (int x = x0; x < x1; x += 2)
                    row[x] = color;
                count += (uint32_t)(x1 - x0 + 1) / 2u;
                continue;
            }
            int32_t zf = (int32_t)(ATTR(&t, a.z, dzx, dzy, x0, y) * ZSCALE);
            uint16_t *zrow = zbuf + (uint32_t)y * g->w;
            for (int x = x0; x < x1; x += 2, zf += dz) {
                const uint32_t zz = zsat(zf);
                if (zz > zrow[x]) {
                    zrow[x] = (uint16_t)zz;
                    row[x] = color;
                    count++;
                }
            }
        });
        return count;
    }
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

/* 4x4 ordered dither, 0..15: the colour steps of RGB565 (8 levels of red
 * and blue, 4 of green) become a fine pattern instead of bands. Per pixel
 * the offsets are (d >> 1, d >> 2, d >> 1), packed here. */
static const uint8_t bayer4[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

/* The same offsets for a colour packed in one 32-bit number (raster_gouraud):
 * red in bits 22..31 (8-bit value x 4), green in 11..21 (x 8), blue in 0..10
 * (x 8): the top 5, 6, 5 bits of each field are the RGB565 levels. */
#define DPK(d) ((uint32_t)((d) >> 1) * 4u << 22 | (uint32_t)((d) >> 2) * 8u << 11 | (uint32_t)((d) >> 1) * 8u)
static const uint32_t bayer4_pk[4][4] = {
    { DPK(0), DPK(8), DPK(2), DPK(10) }, { DPK(12), DPK(4), DPK(14), DPK(6) },
    { DPK(3), DPK(11), DPK(1), DPK(9) }, { DPK(15), DPK(7), DPK(13), DPK(5) },
};

/* r, g, b: 0..248 / 0..252 / 0..248 (the vertex colours are limited so that
 * the dither never overflows: 248 + 7 still rounds to the top level) */
static inline uint16_t dither565(uint32_t r, uint32_t g, uint32_t b, uint32_t d)
{
    r += d >> 1; g += d >> 2; b += d >> 1;
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

/* Gouraud triangle: the colour (r, g, b in 0..255) is interpolated across
 * the face, in screen space, and dithered. With zbuf == NULL, a 2D fill. */
typedef struct { float x, y, z, r, g, b; } gv_t;

static inline float climit(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* a rounded float -> int (no libm) */
static inline int32_t iround(float f)
{
    return (int32_t)(f + (f < 0 ? -0.5f : 0.5f));
}

#ifdef R3D_STATS
uint32_t r3d_stat_visited, r3d_stat_spans, r3d_stat_tris;
#endif

static uint32_t raster_gouraud(g16_t *g, uint16_t *zbuf, gv_t a, gv_t b, gv_t c, int screen)
{
#ifdef R3D_STATS
    r3d_stat_tris++;
#endif
    SORT3(gv_t, a, b, c);
    tri_t t;
    if (!tri_setup(&t, g, a.x, a.y, b.x, b.y, c.x, c.y))
        return 0;
    /* a little in from the ends: the packed colour below never carries
     * from one field to the next (the dither on top, the drift of the step) */
    a.r = climit(a.r, 2.5f, 246.5f); b.r = climit(b.r, 2.5f, 246.5f); c.r = climit(c.r, 2.5f, 246.5f);
    a.g = climit(a.g, 1.5f, 251.5f); b.g = climit(b.g, 1.5f, 251.5f); c.g = climit(c.g, 1.5f, 251.5f);
    a.b = climit(a.b, 1.5f, 247.5f); b.b = climit(b.b, 1.5f, 247.5f); c.b = climit(c.b, 1.5f, 247.5f);
    float drx, dry, dgx, dgy, dbx, dby, dzx = 0, dzy = 0;
    tri_grad(&t, a.r, b.r, c.r, &drx, &dry);
    tri_grad(&t, a.g, b.g, c.g, &dgx, &dgy);
    tri_grad(&t, a.b, b.b, c.b, &dbx, &dby);
    if (zbuf)
        tri_grad(&t, a.z, b.z, c.z, &dzx, &dzy);
    /* 16.16 fixed point across the spans (two pixels a step for screen-door) */
    const int step = screen ? 2 : 1;
    const int32_t ir = (int32_t)(drx * 65536.0f) * step, ig = (int32_t)(dgx * 65536.0f) * step,
                  ib = (int32_t)(dbx * 65536.0f) * step, dzf = (int32_t)(dzx * ZSCALE) * step;
    uint32_t count = 0;
    /* the packed step (bayer4_pk) and 16 steps exact */
    const uint32_t pstep = ((uint32_t)iround(drx * 4.0f) << 22) + ((uint32_t)iround(dgx * 8.0f) << 11) +
                           (uint32_t)iround(dbx * 8.0f);
    const int32_t ir16 = (int32_t)(drx * 65536.0f * 16.0f), ig16 = (int32_t)(dgx * 65536.0f * 16.0f),
                  ib16 = (int32_t)(dbx * 65536.0f * 16.0f);
    /* the attributes of a row start from a base: one multiply a span */
    float br = tri_base(&t, a.r, drx, dry), bg = tri_base(&t, a.g, dgx, dgy), bb = tri_base(&t, a.b, dbx, dby);
    float bz = zbuf ? tri_base(&t, a.z, dzx, dzy) : 0;
    br += t.y0 * dry; bg += t.y0 * dgy; bb += t.y0 * dby; bz += t.y0 * dzy;
    int ry = t.y0;                      /* the row of br, bg, bb, bz */

    SCAN(&t, g, {
        for (; ry < y; ry++) {
            br += dry; bg += dgy; bb += dby; bz += dzy;
        }
        if (screen && ((x0 + y) & 1) && ++x0 >= x1)
            continue;
        /* the edge rounding can step a hair outside the vertex range:
         * clamped as integers */
        const float fx = (float)x0;
        int32_t cr = (int32_t)((br + fx * drx) * 65536.0f);
        int32_t cg = (int32_t)((bg + fx * dgx) * 65536.0f);
        int32_t cb = (int32_t)((bb + fx * dbx) * 65536.0f);
        cr = cr < 163840 ? 163840 : cr > 16154624 ? 16154624 : cr;         /* 2.5 .. 246.5 */
        cg = cg < 98304 ? 98304 : cg > 16482304 ? 16482304 : cg;           /* 1.5 .. 251.5 */
        cb = cb < 98304 ? 98304 : cb > 16220160 ? 16220160 : cb;           /* 1.5 .. 247.5 */
        const uint8_t *dith = bayer4[y & 3];
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        if (!zbuf) {
            for (int x = x0; x < x1; x += step, cr += ir, cg += ig, cb += ib)
                row[x] = dither565((uint32_t)cr >> 16, (uint32_t)cg >> 16, (uint32_t)cb >> 16, dith[x & 3]);
            count += (uint32_t)(x1 - x0 + step - 1) / (uint32_t)step;
            continue;
        }
#ifdef R3D_STATS
        r3d_stat_visited += (uint32_t)(x1 - x0);
        r3d_stat_spans++;
#endif
        int32_t zf = (int32_t)((bz + fx * dzx) * ZSCALE);
        uint16_t *zrow = zbuf + (uint32_t)y * g->w;
        if (step == 1) {
            /* the common case, kept small for the registers of the ARM1176:
             * the colour packed in one number (bayer4_pk), stepped as one;
             * the rounding of its step drifts, so it is packed again from
             * the exact values every 16 pixels (by then at most 8/32 of a
             * level off: the margins of climit); the dither by the address
             * (the rows of a screen start on 8 bytes; if not, the pattern
             * is only shifted) */
            const uint32_t *dpk = bayer4_pk[y & 3];
#if defined(__GNUC__)
            __asm__("" : "+r"(dpk));        /* one register (not rebuilt every pixel) */
#endif
            uint16_t *pp = row + x0, *zp = zrow + x0, *const pend = row + x1;
            while (pp < pend) {
                uint32_t c = ((uint32_t)cr >> 14) << 22 | ((uint32_t)cg >> 13) << 11 | ((uint32_t)cb >> 13);
                uint16_t *const e = pend - pp > 16 ? pp + 16 : pend;
                for (; pp < e; pp++, zp++, zf += dzf, c += pstep) {
                    const uint32_t zz = zsat(zf);
                    if (zz > *zp) {
                        *zp = (uint16_t)zz;
                        const uint32_t d = c + *(const uint32_t *)((const uint8_t *)dpk + (((uintptr_t)pp & 6) << 1));
                        *pp = (uint16_t)((d >> 27) << 11 | ((d << 10) >> 26) << 5 | (d << 21) >> 27);
                    }
                }
                cr += ir16; cg += ig16; cb += ib16;
            }
            count += (uint32_t)(x1 - x0);
            continue;
        }
        for (int x = x0; x < x1; x += step, zf += dzf, cr += ir, cg += ig, cb += ib) {
            const uint32_t zz = zsat(zf);
            if (zz > zrow[x]) {
                zrow[x] = (uint16_t)zz;
                row[x] = dither565((uint32_t)cr >> 16, (uint32_t)cg >> 16, (uint32_t)cb >> 16, dith[x & 3]);
                count++;
            }
        }
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
 * the texture (the general case, as before M33) */
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
    int run;                    /* pixels between exact divisions: TEX_RUN, or more for a face
                                 * whose depth hardly changes */
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
    /* texel coordinates (16.16) exact every run pixels, linear in between */
    float iz = 1.0f / (z > 1e-9f ? z : 1e-9f);
    int32_t uf = (int32_t)(u * iz * 65536.0f), vf = (int32_t)(v * iz * 65536.0f);
    for (int x = 0; x < len;) {
        int n = len - x < gr->run ? len - x : gr->run;
        z += gr->dzx * n; u += gr->dux * n; v += gr->dvx * n;
        float iz2 = 1.0f / (z > 1e-9f ? z : 1e-9f);
        int32_t uf2 = (int32_t)(u * iz2 * 65536.0f), vf2 = (int32_t)(v * iz2 * 65536.0f);
        int32_t duf, dvf;
        if (n == TEX_RUN) {
            duf = (uf2 - uf) / TEX_RUN; dvf = (vf2 - vf) / TEX_RUN;
        } else {
            const float in = n < TEX_RUN ? recip[n] : 1.0f / (float)n;
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

/*
 * A textured triangle. lrgb: the light baked at its corners (a "lit" model:
 * one colour, 128 = 1, up to 2) instead of k, with the fog (0..256) of the
 * face; screen: screen-door, every other pixel.
 */
static uint32_t raster_tex(g16_t *g, uint16_t *zbuf, tv_t a, tv_t b, tv_t c,
                           const g16_sheet_t *tex, int screen, const uint8_t *lrgb, uint32_t fog_rgb, int fog)
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
    const int32_t dkf = flat_k ? 0 : (int32_t)(dkx * 65536.0f), dzf = (int32_t)(dzx * ZSCALE);
    uint32_t count = 0;
    /* a face whose depth changes little (far, or seen square on): one exact
     * division at each end of a span, linear between (off by less than a
     * texel); else every TEX_RUN pixels */
    const float zlo = a.z < b.z ? (a.z < c.z ? a.z : c.z) : (b.z < c.z ? b.z : c.z),
                zhi = a.z > b.z ? (a.z > c.z ? a.z : c.z) : (b.z > c.z ? b.z : c.z);
    const int run = zhi - zlo < zhi * (1.0f / 32) ? 4096 : TEX_RUN;

    if (!lrgb && !screen) {
        /* the specialized runs (no clamp where the run stays inside the
         * sheet, no alpha where its cells are opaque, the light folded) */
        const texgrad_t gr = { dzx, dux, dvx, dzf, dkf,
                               flat_k ? ((int32_t)(a.k * 65536.0f) >> 8 >= 256 ? TL_NONE : TL_FLAT) : TL_SMOOTH,
                               run };
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

    const int tw = tex->w, th = tex->h;
    int tshift = 0;
    while ((1 << tshift) < tw) tshift++;
    const int fast = lrgb && zbuf && !screen && (1 << tshift) == tw;
    const uint16_t *const px = tex->px;
    const uint8_t *const alpha = tex->alpha;
    /* baked light: the fog (0..256) takes its share off the light and adds
     * its colour */
    uint32_t l0 = 0, l1 = 0, l2 = 0, f0 = 0, f1 = 0, f2 = 0;
    if (lrgb) {
        l0 = lrgb[0] * (uint32_t)(256 - fog) >> 8;
        l1 = lrgb[1] * (uint32_t)(256 - fog) >> 8;
        l2 = lrgb[2] * (uint32_t)(256 - fog) >> 8;
        f0 = (fog_rgb >> 16 & 255) * (uint32_t)fog >> 11;
        f1 = (fog_rgb >> 8 & 255) * (uint32_t)fog >> 10;
        f2 = (fog_rgb & 255) * (uint32_t)fog >> 11;
    }

    SCAN(&t, g, {
        float z = ATTR(&t, a.z, dzx, dzy, x0, y), u = ATTR(&t, a.u, dux, duy, x0, y),
              v = ATTR(&t, a.v, dvx, dvy, x0, y);
        int32_t kf = (int32_t)((flat_k ? a.k : ATTR(&t, a.k, dkx, dky, x0, y)) * 65536.0f);
        int32_t zf = (int32_t)(z * ZSCALE);
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        uint16_t *zrow = zbuf ? zbuf + (uint32_t)y * g->w : NULL;
        /* texel coordinates (16.16) exact every run pixels, linear in between */
        float iz = 1.0f / (z > 1e-9f ? z : 1e-9f);
        int32_t uf = (int32_t)(u * iz * 65536.0f), vf = (int32_t)(v * iz * 65536.0f);
        for (int x = x0; x < x1;) {
            int n = x1 - x < run ? x1 - x : run;
            z += dzx * n; u += dux * n; v += dvx * n;
            float iz2 = 1.0f / (z > 1e-9f ? z : 1e-9f);
            int32_t uf2 = (int32_t)(u * iz2 * 65536.0f), vf2 = (int32_t)(v * iz2 * 65536.0f);
            int32_t duf, dvf;
            if (n == TEX_RUN) {
                duf = (uf2 - uf) / TEX_RUN; dvf = (vf2 - vf) / TEX_RUN;
            } else {
                const float in = n < TEX_RUN ? recip[n] : 1.0f / (float)n;
                duf = (int32_t)((float)(uf2 - uf) * in); dvf = (int32_t)((float)(vf2 - vf) * in);
            }
            if (fast) {
                /* the world of a map: baked light, a z-buffer, a sheet as
                 * wide as a power of two */
                for (int e = x + n; x < e; x++, zf += dzf, uf += duf, vf += dvf) {
                    const uint32_t zz = zsat(zf);
                    if (zz <= zrow[x])
                        continue;
                    int tx = uf >> 16, ty = vf >> 16;
                    if ((unsigned)tx >= (unsigned)tw) tx = tx < 0 ? 0 : tw - 1;
                    if ((unsigned)ty >= (unsigned)th) ty = ty < 0 ? 0 : th - 1;
                    const uint32_t i = (uint32_t)ty << tshift | (uint32_t)tx;
                    if (!alpha[i])
                        continue;
                    const uint32_t p = px[i];
                    uint32_t rr = ((p >> 11) * l0 >> 7) + f0, gg = ((p >> 5 & 63) * l1 >> 7) + f1,
                             bb = ((p & 31) * l2 >> 7) + f2;
                    rr = rr > 31 ? 31 : rr; gg = gg > 63 ? 63 : gg; bb = bb > 31 ? 31 : bb;
                    zrow[x] = (uint16_t)zz;
                    row[x] = (uint16_t)(rr << 11 | gg << 5 | bb);
                }
                count += (uint32_t)n;               /* pixels visited (an estimate) */
                uf = uf2; vf = vf2;
                continue;
            }
            for (int e = x + n; x < e; x++, zf += dzf, uf += duf, vf += dvf, kf += dkf) {
                const uint32_t zz = zsat(zf);
                if (zrow && zz <= zrow[x])
                    continue;
                if (screen && ((x + y) & 1))
                    continue;
                int tx = uf >> 16, ty = vf >> 16;
                if ((unsigned)tx >= (unsigned)tw) tx = tx < 0 ? 0 : tw - 1;
                if ((unsigned)ty >= (unsigned)th) ty = ty < 0 ? 0 : th - 1;
                uint32_t i = (uint32_t)ty * (uint32_t)tw + (uint32_t)tx;
                if (!alpha[i])
                    continue;
                uint32_t p = px[i];
                if (lrgb) {
                    uint32_t rr = ((p >> 11) * l0 >> 7) + f0, gg = ((p >> 5 & 63) * l1 >> 7) + f1,
                             bb = ((p & 31) * l2 >> 7) + f2;
                    p = (rr > 31 ? 31 : rr) << 11 | (gg > 63 ? 63 : gg) << 5 | (bb > 31 ? 31 : bb);
                } else {
                    int32_t k = kf >> 8;
                    if (k < 256)
                        p = mod565(p, k <= 0 ? 0 : (uint32_t)k);
                }
                if (zrow)
                    zrow[x] = (uint16_t)zz;
                row[x] = (uint16_t)p;
                count++;
            }
            uf = uf2; vf = vf2;
        }
    });
    return count;
}

void g16_tri(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    sv_t a = { (float)(x0 - g->cam_x), (float)(y0 - g->cam_y), 0 };
    sv_t b = { (float)(x1 - g->cam_x), (float)(y1 - g->cam_y), 0 };
    sv_t d = { (float)(x2 - g->cam_x), (float)(y2 - g->cam_y), 0 };
    raster(g, NULL, a, b, d, c, 0);
}

void g16_tri_gouraud(g16_t *g, int x0, int y0, int x1, int y1, int x2, int y2,
                     uint32_t c0, uint32_t c1, uint32_t c2)
{
#define GV(x, y, c) (gv_t){ (float)((x) - g->cam_x), (float)((y) - g->cam_y), 0, \
                            (float)((c) >> 16 & 0xFF), (float)((c) >> 8 & 0xFF), (float)((c) & 0xFF) }
    raster_gouraud(g, NULL, GV(x0, y0, c0), GV(x1, y1, c1), GV(x2, y2, c2), 0);
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
    r3d_sky(r, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
    r3d_shine(r, 0.6f, 16, 0);
    r3d_zclear(r);
    return 0;
}

void r3d_free(r3d_t *r)
{
    free(r->zbuf);
    free(r->mask);
    r->zbuf = NULL;
    r->mask = NULL;
}

void r3d_zclear(r3d_t *r)
{
    if (r->backend)
        r->backend->zclear(r->backend->ctx, r->g);
    else
        memset(r->zbuf, 0, (size_t)r->g->w * r->g->h * 2);
    r->tris_in = r->tris_drawn = r->pixels = r->verts = 0;
}

/* What follows needs the software rasterizer (it reads the z-buffer, or a
 * material the backend cannot draw): the backend hands over, through the
 * runtime's hook (what it holds goes to the screen first), and the ARM
 * draws from here on. The depth of what the backend drew is not in the
 * z-buffer: at most one frame is drawn out of order. */
static void to_arm(r3d_t *r, const char *why)
{
    if (!r->backend)
        return;
    if (r->arm_hook)
        r->arm_hook(r->arm_ctx, why);
    r->backend = NULL;
    memset(r->zbuf, 0, (size_t)r->g->w * r->g->h * 2);
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

static r3d_rgb_t rgbf(uint32_t c)
{
    return (r3d_rgb_t){ (c >> 16 & 0xFF) / 255.0f, (c >> 8 & 0xFF) / 255.0f, (c & 0xFF) / 255.0f };
}

void r3d_lamp_rgb(r3d_t *r, int i, float x, float y, float z, float radius, float k, uint32_t rgb)
{
    if (i < 0 || i >= R3D_LAMPS)
        return;
    r->lamp[i].pos = (v3_t){ x, y, z };
    r->lamp[i].r2 = radius * radius;
    r->lamp[i].k = k;
    r->lamp[i].c = rgbf(rgb);
    r->lamp[i].on = radius > 0;
}

void r3d_lamp(r3d_t *r, int i, float x, float y, float z, float radius, float k)
{
    r3d_lamp_rgb(r, i, x, y, z, radius, k, 0xFFFFFF);
}

void r3d_light(r3d_t *r, float x, float y, float z, float ambient)
{
    float l = sqrtf(x * x + y * y + z * z);
    if (l < 1e-6f) l = 1;
    r->light = (v3_t){ x / l, y / l, z / l };
    r->ambient = ambient < 0 ? 0 : ambient > 1 ? 1 : ambient;
}

void r3d_sky(r3d_t *r, uint32_t sun, uint32_t sky, uint32_t ground)
{
    r->sun = rgbf(sun);
    r->sky = rgbf(sky);
    r->ground = rgbf(ground);
}

void r3d_shine(r3d_t *r, float spec_k, int exponent, float rim_k)
{
    r->spec_k = spec_k < 0 ? 0 : spec_k > 2 ? 2 : spec_k;
    int s = 2;
    while (s < 6 && (1 << s) < exponent)
        s++;
    r->spec_shift = s;
    r->rim_k = rim_k < 0 ? 0 : rim_k > 1 ? 1 : rim_k;
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

/* Light of a face or vertex: L multiplies the colour, S (specular) is added
 * to it, both 0..1 per channel (L can pass 1). */
typedef struct { r3d_rgb_t l, s; } lit_t;

/* colour (0xRRGGBB) lit, then fogged: fraction f of the fog colour; r, g, b
 * in 0..255 */
static void shade_rgb(uint32_t rgb, const lit_t *k, uint32_t fog, float f, float out[3])
{
    float r = (rgb >> 16 & 0xFF) * k->l.r + k->s.r * 255.0f, g = (rgb >> 8 & 0xFF) * k->l.g + k->s.g * 255.0f,
          b = (rgb & 0xFF) * k->l.b + k->s.b * 255.0f;
    if (f > 0) {
        r += ((fog >> 16 & 0xFF) - r) * f;
        g += ((fog >> 8 & 0xFF) - g) * f;
        b += ((fog & 0xFF) - b) * f;
    }
    out[0] = r > 255 ? 255 : r;
    out[1] = g > 255 ? 255 : g;
    out[2] = b > 255 ? 255 : b;
}

static uint16_t shade(uint32_t rgb, const lit_t *k, uint32_t fog, float f)
{
    float c[3];
    shade_rgb(rgb, k, fog, f, c);
    return g16_rgb((uint32_t)c[0], (uint32_t)c[1], (uint32_t)c[2]);
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

/* zmul: 1, or 0.1 for R3D_FRONT (depths exact from 0.1 units, not 1) */
static sv_t project(const view_t *v, cv_t c, float zmul)
{
    float iz = 1.0f / c.z;
    return (sv_t){ v->hw + c.x * v->f * iz, v->hh - c.y * v->f * iz, iz * zmul };
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
/* triangles smaller than this (twice the area, in pixels) are drawn in one
 * colour: below it the setup of the colour gradients costs more than the
 * pixels, and a gradient over a dozen pixels does not show */
#define SMALL_TRI 48.0f

/* the first six fields of a corner (l and f only for R3D_KIND_TEX_RGB:
 * not written, as a compound literal would) */
static inline void corner(r3d_corner_t *q, float x, float y, float z, float a, float b, float c)
{
    q->x = x; q->y = y; q->z = z; q->a = a; q->b = b; q->c = c;
}

/* a triangle, or the two of a quad left by the near plane, to the backend */
static void emit(r3d_t *r, const r3d_corner_t *q, int np, int kind, const g16_sheet_t *tex, int nodepth)
{
    r->backend->tri(r->backend->ctx, r->g, q, kind, tex, nodepth);
    if (np == 4) {
        const r3d_corner_t q2[3] = { q[0], q[2], q[3] };
        r->backend->tri(r->backend->ctx, r->g, q2, kind, tex, nodepth);
    }
}

/* corners of colour (r, g, b in 0..255) to the backend */
static void emit_colour(r3d_t *r, const sv_t *const *ps, const float (*col)[3], int np, int kind, int depth)
{
    r3d_corner_t q[4];
    for (int i = 0; i < np; i++)
        corner(&q[i], ps[i]->x, ps[i]->y, ps[i]->z, col[i][0] * (1.0f / 255.0f), col[i][1] * (1.0f / 255.0f),
               col[i][2] * (1.0f / 255.0f));
    emit(r, q, np, kind, NULL, depth);
}

void r3d_draw(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz, float scale)
{
    r3d_draw_flags(r, m, p, rx, ry, rz, scale, 0);
}

typedef struct {
    v3_t pos[R3D_LAMPS];        /* in camera space (distances are the same) */
    float r2[R3D_LAMPS], k[R3D_LAMPS];
    r3d_rgb_t c[R3D_LAMPS];
    int n;
} lamps_t;

/* Gouraud colour of a vertex: face colour lit, then fog. */
static void vertex_rgb(cv_t *c, uint32_t rgb, const lit_t *k, int glossy, uint32_t fog, float f)
{
    float r = (rgb >> 16 & 0xFF) * k->l.r, g = (rgb >> 8 & 0xFF) * k->l.g, b = (rgb & 0xFF) * k->l.b;
    if (glossy) {
        r += k->s.r * 255.0f;
        g += k->s.g * 255.0f;
        b += k->s.b * 255.0f;
    }
    if (f > 0) {
        r += ((fog >> 16 & 0xFF) - r) * f;
        g += ((fog >> 8 & 0xFF) - g) * f;
        b += ((fog & 0xFF) - b) * f;
    }
    c->r = r > 255 ? 255 : r;
    c->g = g > 255 ? 255 : g;
    c->b = b > 255 ? 255 : b;
}

static float lit_grey(const lit_t *k)
{
    float v = (k->l.r + k->l.g + k->l.b) * (1.0f / 3.0f);
    return v > 1 ? 1 : v;
}

/* ---- the transform of a draw: object -> world (relative to the camera),
 * and object normals -> world; with a skeleton, one matrix per bone */
#define MAX_BONES 64

typedef struct {
    float m[MAX_BONES + 1][12];     /* rotation (3x3, scaled) and move, per bone; [0] without bones */
    float n[MAX_BONES + 1][9];      /* the rotation alone, for normals */
    const r3d_mesh_t *mesh;
} xform_t;

static void xform_setup(xform_t *x, const r3d_t *r, const r3d_mesh_t *m, const float R[9], v3_t p, float scale)
{
    x->mesh = m;
    const v3_t t = { p.x - r->cam_pos.x, p.y - r->cam_pos.y, p.z - r->cam_pos.z };
    if (!m->bones || m->nbones <= 0) {
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < 3; j++) {
                x->m[0][k * 4 + j] = R[k * 3 + j] * scale;
                x->n[0][k * 3 + j] = R[k * 3 + j];
            }
        }
        x->m[0][3] = t.x; x->m[0][7] = t.y; x->m[0][11] = t.z;
        return;
    }
    const int nb = m->nbones < MAX_BONES ? m->nbones : MAX_BONES;
    for (int b = 0; b < nb; b++) {
        const float *B = m->bones[b];
        float *W = x->m[b], *N = x->n[b];
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < 3; j++) {
                float v = R[k * 3] * B[j] + R[k * 3 + 1] * B[4 + j] + R[k * 3 + 2] * B[8 + j];
                N[k * 3 + j] = v;
                W[k * 4 + j] = v * scale;
            }
            W[k * 4 + 3] = (R[k * 3] * B[3] + R[k * 3 + 1] * B[7] + R[k * 3 + 2] * B[11]) * scale;
        }
        W[3] += t.x; W[7] += t.y; W[11] += t.z;
    }
}

/* vertex i, world axes, relative to the camera */
static inline v3_t xform_vert(const xform_t *x, int i)
{
    const r3d_mesh_t *m = x->mesh;
    const float *W = x->m[m->bones && m->nbones > 0 ? m->vbone[i] : 0];
    v3_t o = m->verts[i];
    return (v3_t){ W[0] * o.x + W[1] * o.y + W[2] * o.z + W[3], W[4] * o.x + W[5] * o.y + W[6] * o.z + W[7],
                   W[8] * o.x + W[9] * o.y + W[10] * o.z + W[11] };
}

/* a normal of the object turned to world axes, with the bone of vertex i */
static inline v3_t xform_dir(const xform_t *x, int i, v3_t n)
{
    const r3d_mesh_t *m = x->mesh;
    const float *N = x->n[m->bones && m->nbones > 0 ? m->vbone[i] : 0];
    return (v3_t){ N[0] * n.x + N[1] * n.y + N[2] * n.z, N[3] * n.x + N[4] * n.y + N[5] * n.z,
                   N[6] * n.x + N[7] * n.y + N[8] * n.z };
}

/* ---- shadows: the lit faces of the mesh flattened on a plane, into a
 * mask (where the ground there is not hidden by something nearer), then the
 * masked pixels darkened once */

static int mask_ready(r3d_t *r)
{
    if (!r->mask)
        r->mask = calloc((size_t)r->g->w * r->g->h, 1);
    return r->mask != NULL;
}

static void raster_mask(r3d_t *r, sv_t a, sv_t b, sv_t c, int box[4])
{
    g16_t *g = r->g;
    SORT3(sv_t, a, b, c);
    tri_t t;
    if (!tri_setup(&t, g, a.x, a.y, b.x, b.y, c.x, c.y))
        return;
    float dzx, dzy;
    tri_grad(&t, a.z, b.z, c.z, &dzx, &dzy);
    const int32_t dzf = (int32_t)(dzx * ZSCALE);
    SCAN(&t, g, {
        int32_t zf = (int32_t)(ATTR(&t, a.z, dzx, dzy, x0, y) * ZSCALE);
        const uint16_t *zrow = r->zbuf + (uint32_t)y * g->w;
        uint8_t *mrow = r->mask + (uint32_t)y * g->w;
        for (int x = x0; x < x1; x++, zf += dzf) {
            int32_t zz = zf >> 8, zb = zrow[x];
            if (zz > 65535) zz = 65535;
            if (zz + (zb >> 5) + 8 >= zb)       /* not behind what is drawn there */
                mrow[x] = 1;
        }
        if (x0 < box[0]) box[0] = x0;
        if (x1 > box[2]) box[2] = x1;
        if (y < box[1]) box[1] = y;
        if (y + 1 > box[3]) box[3] = y + 1;
    });
}

static void shadow_apply(r3d_t *r, const int box[4])
{
    g16_t *g = r->g;
    for (int y = box[1]; y < box[3]; y++) {
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        uint8_t *mrow = r->mask + (uint32_t)y * g->w;
        for (int x = box[0]; x < box[2]; x++) {
            if (!mrow[x])
                continue;
            mrow[x] = 0;
            if (r->shadow_style) {
                if (!((x + y) & 1))
                    row[x] = 0;
            } else {
                uint16_t p = row[x];
                row[x] = (uint16_t)(((p >> 1) & 0x7BEF) + ((p >> 3) & 0x18E3));
            }
            r->pixels++;
        }
    }
}

static void draw_shadow(r3d_t *r, const r3d_mesh_t *m, const xform_t *x, v3_t p, unsigned detail, const view_t *v)
{
    static cv_t cv[MAX_VERTS];
    static sv_t sv[MAX_VERTS];              /* on the screen, if in front of the near plane */
    if (!r->backend && !mask_ready(r))
        return;
    v3_t L = r->light;
    if (L.y < 0.25f) L.y = 0.25f;           /* a low sun: shadows not longer than 4x */
    const float *C = v->c;
    const unsigned dbit = 1u << detail;
    const float plane = p.y - r->cam_pos.y;
    for (int i = 0; i < m->nverts; i++) {
        if (m->vlod && !(m->vlod[i] & dbit))
            continue;
        v3_t w = xform_vert(x, i);
        float h = (w.y - plane) / L.y;      /* along the light down to the plane */
        if (h < 0) h = 0;
        w.x -= L.x * h;
        w.y = plane + 0.01f;
        w.z -= L.z * h;
        cv[i] = (cv_t){ C[0] * w.x + C[1] * w.y + C[2] * w.z, C[3] * w.x + C[4] * w.y + C[5] * w.z,
                        C[6] * w.x + C[7] * w.y + C[8] * w.z, 0, 0, 0, 0, 0 };
        if (cv[i].z >= NEAR)
            sv[i] = project(v, cv[i], 1.0f);
    }
    /* the sun in the axes of each bone: a face is lit when its normal at
     * rest points to it, n . (N^T sun) = (N n) . sun */
    static v3_t sun[MAX_BONES + 1];
    const int skinned = m->bones && m->nbones > 0;
    const int nb = skinned ? (m->nbones < MAX_BONES ? m->nbones : MAX_BONES) : 1;
    for (int b = 0; b < nb; b++) {
        const float *N = x->n[b];
        sun[b] = (v3_t){ N[0] * r->light.x + N[3] * r->light.y + N[6] * r->light.z,
                         N[1] * r->light.x + N[4] * r->light.y + N[7] * r->light.z,
                         N[2] * r->light.x + N[5] * r->light.y + N[8] * r->light.z };
    }
    int box[4] = { r->g->w, r->g->h, 0, 0 };
    for (int t = 0; t < m->nfaces; t++) {
        if (!R3D_LOD_SHOWS(m->colors[t], detail))
            continue;
        const uint16_t *fc = m->faces + t * 3;
        const v3_t n = m->normals[t], sb = sun[skinned ? m->vbone[fc[0]] : 0];
        if (n.x * sb.x + n.y * sb.y + n.z * sb.z <= 0 || (m->colors[t] & R3D_SCREEN))
            continue;                       /* only the faces the sun sees cast the shadow */
        sv_t pts[4];
        int np = 3;
        if (cv[fc[0]].z >= NEAR && cv[fc[1]].z >= NEAR && cv[fc[2]].z >= NEAR) {
            pts[0] = sv[fc[0]];             /* (as clip_near and project would) */
            pts[1] = sv[fc[1]];
            pts[2] = sv[fc[2]];
        } else {
            cv_t tri[3] = { cv[fc[0]], cv[fc[1]], cv[fc[2]] }, cl[4];
            np = clip_near(tri, cl);
            if (np < 3)
                continue;
            for (int i = 0; i < np; i++)
                pts[i] = project(v, cl[i], 1.0f);
        }
        if (r->backend) {
            /* the GPU: black on every other pixel (shadow3d(1)), where the
             * ground is not much nearer than the shadow (its depth 3.5%
             * nearer), the depth not written: overlaps stay the same */
            r3d_corner_t q[4];
            for (int i = 0; i < np; i++)
                corner(&q[i], pts[i].x, pts[i].y, pts[i].z * 1.035f, 0, 0, 0);
            emit(r, q, np, R3D_KIND_SCREEN, NULL, R3D_DEPTH_TEST);
            continue;
        }
        raster_mask(r, pts[0], pts[1], pts[2], box);
        if (np == 4)
            raster_mask(r, pts[0], pts[2], pts[3], box);
    }
    if (box[2] > box[0] && !r->backend)
        shadow_apply(r, box);
}

/* The light of a whole object seen from the camera: rim light and
 * highlights use one view direction for all its vertices (an object is
 * small next to its distance), so no square root per vertex. */
typedef struct {
    v3_t V, H;                  /* towards the camera; half way between it and the sun */
} shine_t;

static void light_fast(const r3d_t *r, const lamps_t *L, const shine_t *sh, v3_t n, float px, float py, float pz,
                       int glossy, lit_t *out)
{
    const float d = n.x * r->light.x + n.y * r->light.y + n.z * r->light.z;
    const float dd = d > 0 ? d : 0, a = r->ambient, t = 0.5f + 0.5f * n.y, ka = (1.0f - a) * dd;
    out->l.r = a * (r->ground.r + (r->sky.r - r->ground.r) * t) + ka * r->sun.r;
    out->l.g = a * (r->ground.g + (r->sky.g - r->ground.g) * t) + ka * r->sun.g;
    out->l.b = a * (r->ground.b + (r->sky.b - r->ground.b) * t) + ka * r->sun.b;
    out->s.r = out->s.g = out->s.b = 0;
    for (int i = 0; i < L->n; i++) {
        float dx = px - L->pos[i].x, dy = py - L->pos[i].y, dz = pz - L->pos[i].z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < L->r2[i]) {
            float k = L->k[i] * (1.0f - d2 / L->r2[i]);
            out->l.r += k * L->c[i].r;
            out->l.g += k * L->c[i].g;
            out->l.b += k * L->c[i].b;
        }
    }
    if (r->rim_k > 0) {
        float nv = n.x * sh->V.x + n.y * sh->V.y + n.z * sh->V.z, e = 1.0f - (nv > 0 ? nv : 0);
        float k = r->rim_k * e * e;
        out->l.r += k * r->sky.r;
        out->l.g += k * r->sky.g;
        out->l.b += k * r->sky.b;
    }
    if (glossy && d > 0 && r->spec_k > 0) {
        float nh = n.x * sh->H.x + n.y * sh->H.y + n.z * sh->H.z;
        if (nh > 0) {
            for (int i = 0; i < r->spec_shift; i++)
                nh *= nh;
            float k = nh * r->spec_k;
            out->s.r = k * r->sun.r;
            out->s.g = k * r->sun.g;
            out->s.b = k * r->sun.b;
        }
    }
}

/* the lamps on top of a light (baked faces): by distance only */
static inline __attribute__((always_inline)) void lamps_add(const lamps_t *L, float px, float py, float pz,
                                                          lit_t *out)
{
    for (int i = 0; i < L->n; i++) {
        float dx = px - L->pos[i].x, dy = py - L->pos[i].y, dz = pz - L->pos[i].z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < L->r2[i]) {
            float k = L->k[i] * (1.0f - d2 / L->r2[i]);
            out->l.r += k * L->c[i].r;
            out->l.g += k * L->c[i].g;
            out->l.b += k * L->c[i].b;
        }
    }
}

/* A face with a colour at each corner (r, g, b in 0..255; c, s: its corners
 * in camera space and on the screen, nin of them in front of the near
 * plane): to the backend, or Gouraud on the ARM (one colour for a few
 * pixels). 0 if nothing of it is left after clipping. */
static inline __attribute__((always_inline)) int face_colours(r3d_t *r, const view_t *v, uint16_t *zbuf,
                                                              const cv_t *const c[3], const sv_t *const s[3],
                                                              int nin, const float (*col)[3], float zmul,
                                                              int screen, int inside)
{
    const int kind = (screen ? R3D_KIND_SCREEN : R3D_KIND_COLOUR) | inside;
    if (nin == 3) {
        if (r->backend) {
            emit_colour(r, s, col, 3, kind, zbuf == NULL);
            return 1;
        }
        if ((s[1]->x - s[0]->x) * (s[2]->y - s[0]->y) - (s[1]->y - s[0]->y) * (s[2]->x - s[0]->x) < SMALL_TRI) {
            /* a few pixels: one colour (the setup of the gradients would
             * cost more than the pixels) */
            uint32_t rr = (uint32_t)((col[0][0] + col[1][0] + col[2][0]) * (1.0f / 3)),
                     gg = (uint32_t)((col[0][1] + col[1][1] + col[2][1]) * (1.0f / 3)),
                     bb = (uint32_t)((col[0][2] + col[1][2] + col[2][2]) * (1.0f / 3));
            r->pixels += raster(r->g, zbuf, *s[0], *s[1], *s[2], g16_rgb(rr, gg, bb), screen);
        } else {
            gv_t g0 = { s[0]->x, s[0]->y, s[0]->z, col[0][0], col[0][1], col[0][2] };
            gv_t g1 = { s[1]->x, s[1]->y, s[1]->z, col[1][0], col[1][1], col[1][2] };
            gv_t g2 = { s[2]->x, s[2]->y, s[2]->z, col[2][0], col[2][1], col[2][2] };
            r->pixels += raster_gouraud(r->g, zbuf, g0, g1, g2, screen);
        }
        return 1;
    }
    cv_t tri[3] = { *c[0], *c[1], *c[2] }, cl[4];
    for (int k = 0; k < 3; k++) {
        tri[k].r = col[k][0]; tri[k].g = col[k][1]; tri[k].b = col[k][2];
    }
    int np = clip_near(tri, cl);
    sv_t pts[4];
    for (int i = 0; i < np; i++)
        pts[i] = project(v, cl[i], zmul);
    float area = (pts[1].x - pts[0].x) * (pts[2].y - pts[0].y) - (pts[1].y - pts[0].y) * (pts[2].x - pts[0].x);
    if (area <= 0)
        return 0;
    if (r->backend) {
        const sv_t *ps[4] = { &pts[0], &pts[1], &pts[2], &pts[3] };
        float cc[4][3];
        for (int i = 0; i < np; i++) {
            cc[i][0] = cl[i].r; cc[i][1] = cl[i].g; cc[i][2] = cl[i].b;
        }
        emit_colour(r, ps, (const float (*)[3])cc, np, kind, zbuf == NULL);
        return 1;
    }
    gv_t gv[4];
    for (int i = 0; i < np; i++)
        gv[i] = (gv_t){ pts[i].x, pts[i].y, pts[i].z, cl[i].r, cl[i].g, cl[i].b };
    r->pixels += raster_gouraud(r->g, zbuf, gv[0], gv[1], gv[2], screen);
    if (np == 4)
        r->pixels += raster_gouraud(r->g, zbuf, gv[0], gv[2], gv[3], screen);
    return 1;
}

void r3d_draw_flags(r3d_t *r, const r3d_mesh_t *m, v3_t p, float rx, float ry, float rz,
                    float scale, unsigned flags)
{
    static sv_t sv[MAX_VERTS];
    static cv_t cv[MAX_VERTS];
    static v3_t vn[MAX_VERTS];          /* Gouraud: vertex normals in world axes */
    static float vc[MAX_VERTS][3];      /* Gouraud: the colour of each vertex... */
    static uint32_t vkey[MAX_VERTS];    /* ...for this face colour (computed when a face needs it) */
    if (m->nverts > MAX_VERTS)
        return;

    view_t v;
    view_setup(r, &v);
    const float *C = v.c;
    int inside = 0;                     /* R3D_INSIDE for the backend */
    if (m->radius >= 0 && !(m->bones && m->nbones > 0) && !(flags & R3D_SHADOW)) {
        /* the mesh's bounding sphere, in camera space, against the near
         * plane and the four planes through the eye and the screen edges:
         * a mesh out of view is skipped before its vertices are transformed
         * (not one with a skeleton: its vertices move) */
        const float rad = m->radius * fabsf(scale);
        const float wx = p.x - r->cam_pos.x, wy = p.y - r->cam_pos.y, wz = p.z - r->cam_pos.z;
        const float cx = C[0] * wx + C[1] * wy + C[2] * wz, cy = C[3] * wx + C[4] * wy + C[5] * wz,
                    cz = C[6] * wx + C[7] * wy + C[8] * wz;
        if (cz + rad < NEAR || v.f * fabsf(cx) - v.hw * cz > rad * v.side ||
            v.f * fabsf(cy) - v.hh * cz > rad * v.top) {
            r->tris_in += (uint32_t)m->nfaces;
            return;
        }
        /* the backend's guard band: |x| <= |cx| + rad and z >= cz - rad
         * for every point of the sphere */
        const float zmin = cz - rad, gb = r->backend ? r->backend->guard : 0;
        if (gb > 0 && zmin >= NEAR && v.f * (fabsf(cx) + rad) <= (v.hw + gb) * zmin &&
            v.f * (fabsf(cy) + rad) <= (v.hh + gb) * zmin)
            inside = R3D_INSIDE;
    }
    float R[9];
    rot_matrix(R, rx, ry, rz);
    static xform_t X;
    xform_setup(&X, r, m, R, p, scale);
    const unsigned detail = 3u - (flags >> 4 & 3u), dbit = 1u << detail;
    if (flags & R3D_SHADOW) {
        draw_shadow(r, m, &X, p, detail, &v);
        return;
    }
    uint16_t *zbuf = (flags & R3D_NOZ) ? NULL : r->zbuf;
    const int unlit = (flags & R3D_UNLIT) != 0;
    const int smooth = (flags & R3D_SMOOTH) && m->vnormals;
    const int front = (flags & R3D_FRONT) != 0;
    const float zmul = front ? 0.1f : 1.0f;
    const int fog = r->fog_far > r->fog_near;
    const float fog_k = fog ? 1.0f / (r->fog_far - r->fog_near) : 0;
    /* the lamps in camera space, like the vertices */
    lamps_t lamps = { .n = 0 };
    for (int i = 0; i < R3D_LAMPS && !unlit; i++) {
        if (!r->lamp[i].on)
            continue;
        const float wx = r->lamp[i].pos.x - r->cam_pos.x, wy = r->lamp[i].pos.y - r->cam_pos.y,
                    wz = r->lamp[i].pos.z - r->cam_pos.z;
        lamps.pos[lamps.n] = (v3_t){ C[0] * wx + C[1] * wy + C[2] * wz, C[3] * wx + C[4] * wy + C[5] * wz,
                                     C[6] * wx + C[7] * wy + C[8] * wz };
        lamps.r2[lamps.n] = r->lamp[i].r2;
        lamps.k[lamps.n] = r->lamp[i].k;
        lamps.c[lamps.n++] = r->lamp[i].c;
    }
    /* one view direction for the whole object (its middle, about a unit
     * above where it stands; the camera's own back for a first-person model) */
    shine_t sh;
    {
        float wx = r->cam_pos.x - p.x, wy = r->cam_pos.y - (p.y + (front ? 0 : scale)), wz = r->cam_pos.z - p.z;
        float l = sqrtf(wx * wx + wy * wy + wz * wz);
        if (front || l < 0.3f) {
            sh.V = (v3_t){ -C[6], -C[7], -C[8] };
        } else {
            sh.V = (v3_t){ wx / l, wy / l, wz / l };
        }
        float hx = r->light.x + sh.V.x, hy = r->light.y + sh.V.y, hz = r->light.z + sh.V.z;
        float hl = sqrtf(hx * hx + hy * hy + hz * hz);
        sh.H = hl > 1e-6f ? (v3_t){ hx / hl, hy / hl, hz / hl } : sh.V;
    }

    /* baked light (a "lit" model): the lamps still add to it (flashes, fire) */
    const int baked_lamps = m->clight && lamps.n;
    int bx0 = r->g->w, by0 = r->g->h, bx1 = 0, by1 = 0;     /* R3D_FRONT: screen box */
    int nv = 0, nfront = 0;
    const int skinned = m->bones && m->nbones > 0;
    /* a backend: object -> camera in one matrix a bone (its pixels need not
     * match the ARM's to the last bit; the ARM keeps its two steps) */
    static float F[MAX_BONES + 1][12];
    const int fused = r->backend != NULL;
    if (fused) {
        const int nb = skinned ? (m->nbones < MAX_BONES ? m->nbones : MAX_BONES) : 1;
        for (int b = 0; b < nb; b++) {
            const float *W = X.m[b];
            for (int k = 0; k < 3; k++)
                for (int j = 0; j < 4; j++)
                    F[b][k * 4 + j] = C[k * 3] * W[j] + C[k * 3 + 1] * W[4 + j] + C[k * 3 + 2] * W[8 + j];
        }
    }
    for (int i = 0; i < m->nverts; i++) {
        if (m->vlod && !(m->vlod[i] & dbit))
            continue;                       /* no face of this level of detail needs it */
        nv++;
        const v3_t o = m->verts[i];
        cv_t *c = &cv[i];
        if (fused) {
            const float *M = F[skinned ? m->vbone[i] : 0];
            c->x = M[0] * o.x + M[1] * o.y + M[2] * o.z + M[3];
            c->y = M[4] * o.x + M[5] * o.y + M[6] * o.z + M[7];
            c->z = M[8] * o.x + M[9] * o.y + M[10] * o.z + M[11];
        } else {
            /* object -> world, relative to the camera (with its bone, if any) */
            const float *W = X.m[skinned ? m->vbone[i] : 0];
            const v3_t w = { W[0] * o.x + W[1] * o.y + W[2] * o.z + W[3],
                             W[4] * o.x + W[5] * o.y + W[6] * o.z + W[7],
                             W[8] * o.x + W[9] * o.y + W[10] * o.z + W[11] };
            c->x = C[0] * w.x + C[1] * w.y + C[2] * w.z;
            c->y = C[3] * w.x + C[4] * w.y + C[5] * w.z;
            c->z = C[6] * w.x + C[7] * w.y + C[8] * w.z;
        }
        if (c->z >= NEAR) {
            nfront++;
            float iz = 1.0f / c->z;
            sv[i] = (sv_t){ v.hw + c->x * v.f * iz, v.hh - c->y * v.f * iz, iz * zmul };
            if (front) {
                int x = (int)sv[i].x, y = (int)sv[i].y;
                if (x < bx0) bx0 = x;
                if (x + 1 > bx1) bx1 = x + 1;
                if (y < by0) by0 = y;
                if (y + 1 > by1) by1 = y + 1;
            }
        } else if (front) {
            bx0 = by0 = 0;                  /* through the near plane: the whole screen */
            bx1 = r->g->w;
            by1 = r->g->h;
        }
        if (smooth) {
            vn[i] = xform_dir(&X, i, m->vnormals[i]);
            vkey[i] = 0xFFFFFFFFu;          /* no colour yet */
        }
    }
    r->verts += (uint32_t)nv;
    const int all_front = nfront == nv;     /* every corner of every face shown is in front */
    if (front && zbuf) {
        /* the first-person layer: nothing drawn before can hide it */
        if (r->backend) {
            r->backend->zclear(r->backend->ctx, r->g);
        } else {
            if (bx0 < r->g->cx0) bx0 = r->g->cx0;
            if (by0 < r->g->cy0) by0 = r->g->cy0;
            if (bx1 > r->g->cx1) bx1 = r->g->cx1;
            if (by1 > r->g->cy1) by1 = r->g->cy1;
            for (int y = by0; y < by1; y++)
                memset(zbuf + (uint32_t)y * r->g->w + bx0, 0, (size_t)(bx1 > bx0 ? bx1 - bx0 : 0) * 2);
        }
    }

    static const lit_t full = { { 1, 1, 1 }, { 0, 0, 0 } };
    for (int t = 0; t < m->nfaces; t++) {
        const uint32_t rgb = m->colors[t];
        if (!R3D_LOD_SHOWS(rgb, detail))
            continue;
        const uint16_t *fc = m->faces + t * 3;
        const cv_t *c0 = &cv[fc[0]], *c1 = &cv[fc[1]], *c2 = &cv[fc[2]];
        r->tris_in++;
        const int nin = all_front ? 3 : (c0->z >= NEAR) + (c1->z >= NEAR) + (c2->z >= NEAR);
        if (nin == 0)
            continue;
        /* back faces first, on the vertices in front of the camera (the
         * clipped polygon has the same winding) */
        if (nin == 3) {
            const sv_t *a = &sv[fc[0]], *b = &sv[fc[1]], *c = &sv[fc[2]];
            if ((b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x) <= 0)
                continue;                   /* y points down on screen */
        }
        const int textured = (rgb & R3D_TEXTURED) && m->uv && m->tex;
        const int emissive = unlit || (rgb & R3D_EMISSIVE);
        const int glossy = (rgb & R3D_GLOSSY) != 0;
        const int screen = (rgb & R3D_SCREEN) != 0;
        const cv_t *const cs[3] = { c0, c1, c2 };
        const sv_t *const ss[3] = { &sv[fc[0]], &sv[fc[1]], &sv[fc[2]] };

        if (m->clight && !textured) {
            /* light baked at the corners (the world of a map): the colour of
             * each corner from it, the lamps on top, the fog; smooth */
            const uint8_t *bl = m->clight + t * 9;
            float col[3][3];
            for (int k = 0; k < 3; k++) {
                const cv_t *ck = cs[k];
                lit_t L = { { bl[k * 3] * (1.0f / 128), bl[k * 3 + 1] * (1.0f / 128), bl[k * 3 + 2] * (1.0f / 128) },
                            { 0, 0, 0 } };
                if (emissive)
                    L = full;
                else if (baked_lamps)
                    lamps_add(&lamps, ck->x, ck->y, ck->z, &L);
                float ff = 0;
                if (fog) {
                    ff = (ck->z - r->fog_near) * fog_k;
                    ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
                }
                cv_t tmp;
                vertex_rgb(&tmp, rgb, &L, 0, r->fog_rgb, ff);
                col[k][0] = tmp.r; col[k][1] = tmp.g; col[k][2] = tmp.b;
            }
            r->tris_drawn += (uint32_t)face_colours(r, &v, zbuf, cs, ss, nin, (const float (*)[3])col, zmul,
                                                    screen, inside);
            continue;
        }

        if (smooth && !(rgb & R3D_FLAT) && !textured) {
            /* Gouraud: the colour of each corner, once per vertex and face colour */
            const uint32_t key = rgb & 0x7FFFFFFFu;
            float col[3][3];
            for (int k = 0; k < 3; k++) {
                const int i = fc[k];
                if (vkey[i] != key) {
                    vkey[i] = key;
                    lit_t L;
                    if (emissive)
                        L = full;
                    else
                        light_fast(r, &lamps, &sh, vn[i], cv[i].x, cv[i].y, cv[i].z, glossy, &L);
                    float ff = 0;
                    if (fog) {
                        ff = (cv[i].z - r->fog_near) * fog_k;
                        ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
                    }
                    cv_t tmp;
                    vertex_rgb(&tmp, rgb, &L, glossy, r->fog_rgb, ff);
                    vc[i][0] = tmp.r; vc[i][1] = tmp.g; vc[i][2] = tmp.b;
                }
                col[k][0] = vc[i][0]; col[k][1] = vc[i][1]; col[k][2] = vc[i][2];
            }
            r->tris_drawn += (uint32_t)face_colours(r, &v, zbuf, cs, ss, nin, (const float (*)[3])col, zmul,
                                                    screen, inside);
            continue;
        }

        /* flat (and textured): the light of the face, at its middle (a
         * textured face of a "lit" model has its own, baked) */
        lit_t k = full;
        if (!emissive && !m->clight) {
            const float *N = X.n[skinned ? m->vbone[fc[0]] : 0];
            const v3_t n0 = m->normals[t];
            const v3_t n = { N[0] * n0.x + N[1] * n0.y + N[2] * n0.z, N[3] * n0.x + N[4] * n0.y + N[5] * n0.z,
                             N[6] * n0.x + N[7] * n0.y + N[8] * n0.z };
            float mx = 0, my = 0, mz = 0;
            if (lamps.n) {                  /* the middle, for the lamps */
                mx = (c0->x + c1->x + c2->x) * (1.0f / 3.0f);
                my = (c0->y + c1->y + c2->y) * (1.0f / 3.0f);
                mz = (c0->z + c1->z + c2->z) * (1.0f / 3.0f);
            }
            light_fast(r, &lamps, &sh, n, mx, my, mz, glossy, &k);
        }
        if (textured) {
            /* corner i: on the screen at *ps[i], texel (tu[i], tv_[i]); the
             * light gk; for a "lit" model on the backend, the light baked at
             * the corner (and the lamps) L[i] and its depth dz[i] (fog) */
            if (screen && r->backend)
                /* textured screen-door: the backend cannot; the ARM from
                 * here on (this frame, mixed, is not shown) */
                to_arm(r, "textured screen-door faces");
            const float gk = lit_grey(&k), *uv = m->uv + t * 6;
            const sv_t *ps[4] = { ss[0], ss[1], ss[2], NULL };
            float tu[4] = { uv[0], uv[2], uv[4], 0 }, tv_[4] = { uv[1], uv[3], uv[5], 0 };
            float L[4][3], dz[4] = { c0->z, c1->z, c2->z, 0 };
            const int rgb_light = r->backend && m->clight;
            if (rgb_light) {
                const uint8_t *bl = m->clight + t * 9;
                for (int i = 0; i < 3; i++) {
                    lit_t Lk = { { bl[i * 3] * (1.0f / 128), bl[i * 3 + 1] * (1.0f / 128),
                                   bl[i * 3 + 2] * (1.0f / 128) }, { 0, 0, 0 } };
                    if (baked_lamps)
                        lamps_add(&lamps, cs[i]->x, cs[i]->y, cs[i]->z, &Lk);
                    L[i][0] = Lk.l.r; L[i][1] = Lk.l.g; L[i][2] = Lk.l.b;
                }
            }
            sv_t pts[4];
            int np = 3;
            if (nin < 3) {
                cv_t tri[3] = { *c0, *c1, *c2 }, cl[4];
                for (int i = 0; i < 3; i++) {
                    tri[i].u = uv[i * 2];
                    tri[i].v = uv[i * 2 + 1];
                    if (rgb_light) {
                        tri[i].r = L[i][0]; tri[i].g = L[i][1]; tri[i].b = L[i][2];
                    }
                }
                np = clip_near(tri, cl);
                for (int i = 0; i < np; i++) {
                    pts[i] = project(&v, cl[i], zmul);
                    ps[i] = &pts[i];
                    tu[i] = cl[i].u;
                    tv_[i] = cl[i].v;
                    dz[i] = cl[i].z;
                    L[i][0] = cl[i].r; L[i][1] = cl[i].g; L[i][2] = cl[i].b;
                }
                float area = (pts[1].x - pts[0].x) * (pts[2].y - pts[0].y) - (pts[1].y - pts[0].y) * (pts[2].x - pts[0].x);
                if (area <= 0)
                    continue;
            }
            if (rgb_light) {
                /* texel * light + fog, the GPU's shader: half the light (it
                 * doubles it), the fog's share taken off it */
                const float fr = (r->fog_rgb >> 16 & 255) * (1.0f / 255), fg = (r->fog_rgb >> 8 & 255) * (1.0f / 255),
                            fb = (r->fog_rgb & 255) * (1.0f / 255);
                r3d_corner_t q[4];
                for (int i = 0; i < np; i++) {
                    float ff = 0;
                    if (fog) {
                        ff = (dz[i] - r->fog_near) * fog_k;
                        ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
                    }
                    const float kl = (1.0f - ff) * 0.5f;
                    corner(&q[i], ps[i]->x, ps[i]->y, ps[i]->z, tu[i], tv_[i], 1);
                    q[i].l[0] = L[i][0] * kl; q[i].l[1] = L[i][1] * kl; q[i].l[2] = L[i][2] * kl;
                    q[i].f[0] = fr * ff; q[i].f[1] = fg * ff; q[i].f[2] = fb * ff;
                }
                emit(r, q, np, R3D_KIND_TEX_RGB | inside, m->tex, zbuf == NULL);
                r->tris_drawn++;
                continue;
            }
            if (r->backend) {
                /* (textured screen-door faces never get here: to_arm above) */
                r3d_corner_t q[4];
                for (int i = 0; i < np; i++)
                    corner(&q[i], ps[i]->x, ps[i]->y, ps[i]->z, tu[i], tv_[i], gk);
                emit(r, q, np, R3D_KIND_TEXTURE | inside, m->tex, zbuf == NULL);
                r->tris_drawn++;
                continue;
            }
            tv_t tv[4];
            for (int i = 0; i < np; i++)
                tv[i] = (tv_t){ ps[i]->x, ps[i]->y, ps[i]->z, tu[i] * ps[i]->z, tv_[i] * ps[i]->z, gk };
            const uint8_t *lrgb = m->clight ? m->clight + t * 9 : NULL;
            int tf = 0;
            if (lrgb && fog) {
                float ff = ((c0->z + c1->z + c2->z) * (1.0f / 3.0f) - r->fog_near) * fog_k;
                tf = ff <= 0 ? 0 : ff >= 1 ? 256 : (int)(ff * 256);
            }
            r->pixels += raster_tex(r->g, zbuf, tv[0], tv[1], tv[2], m->tex, screen, lrgb, r->fog_rgb, tf);
            if (np == 4)
                r->pixels += raster_tex(r->g, zbuf, tv[0], tv[2], tv[3], m->tex, screen, lrgb, r->fog_rgb, tf);
            r->tris_drawn++;
            continue;
        }
        float ff = 0;
        if (fog) {
            ff = ((c0->z + c1->z + c2->z) * (1.0f / 3.0f) - r->fog_near) * fog_k;
            ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
        }
        const sv_t *ps[4] = { ss[0], ss[1], ss[2], NULL };
        sv_t pts[4];
        int np = 3;
        if (nin < 3) {
            cv_t tri[3] = { *c0, *c1, *c2 }, cl[4];
            np = clip_near(tri, cl);
            for (int i = 0; i < np; i++) {
                pts[i] = project(&v, cl[i], zmul);
                ps[i] = &pts[i];
            }
            float area = (pts[1].x - pts[0].x) * (pts[2].y - pts[0].y) - (pts[1].y - pts[0].y) * (pts[2].x - pts[0].x);
            if (area <= 0)
                continue;
        }
        if (r->backend) {
            /* the colour straight to the backend (no RGB565 on the way) */
            float c[3];
            shade_rgb(rgb, &k, r->fog_rgb, ff, c);
            const float k255 = 1.0f / 255.0f, cr = c[0] * k255, cg = c[1] * k255, cb = c[2] * k255;
            r3d_corner_t q[4];
            for (int i = 0; i < np; i++)
                corner(&q[i], ps[i]->x, ps[i]->y, ps[i]->z, cr, cg, cb);
            emit(r, q, np, (screen ? R3D_KIND_SCREEN : R3D_KIND_COLOUR) | inside, NULL, zbuf == NULL);
            r->tris_drawn++;
            continue;
        }
        const uint16_t col = shade(rgb, &k, r->fog_rgb, ff);
        r->pixels += raster(r->g, zbuf, *ps[0], *ps[1], *ps[2], col, screen);
        if (np == 4)
            r->pixels += raster(r->g, zbuf, *ps[0], *ps[2], *ps[3], col, screen);
        r->tris_drawn++;
    }
}

/* ---------------------------------------------------------------- effects */

/* a world point in camera space; 0 if it is behind the near plane */
static int to_camera(const r3d_t *r, const view_t *v, v3_t p, cv_t *c)
{
    float wx = p.x - r->cam_pos.x, wy = p.y - r->cam_pos.y, wz = p.z - r->cam_pos.z;
    *c = (cv_t){ v->c[0] * wx + v->c[1] * wy + v->c[2] * wz, v->c[3] * wx + v->c[4] * wy + v->c[5] * wz,
                 v->c[6] * wx + v->c[7] * wy + v->c[8] * wz, 0, 0, 0, 0, 0 };
    return c->z >= NEAR;
}

static uint16_t fog_colour(const r3d_t *r, uint32_t rgb, float depth)
{
    lit_t k = { { 1, 1, 1 }, { 0, 0, 0 } };
    float ff = 0;
    if (r->fog_far > r->fog_near) {
        ff = (depth - r->fog_near) / (r->fog_far - r->fog_near);
        ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
    }
    return shade(rgb, &k, r->fog_rgb, ff);
}

/* the colour of an effect for a backend: rgb (0xRRGGBB) fogged, 0..1 */
static void fog_unit(const r3d_t *r, uint32_t rgb, float depth, float out[3])
{
    static const lit_t k = { { 1, 1, 1 }, { 0, 0, 0 } };
    float ff = 0;
    if (r->fog_far > r->fog_near) {
        ff = (depth - r->fog_near) / (r->fog_far - r->fog_near);
        ff = ff < 0 ? 0 : ff > 1 ? 1 : ff;
    }
    shade_rgb(rgb, &k, r->fog_rgb, ff, out);
    for (int i = 0; i < 3; i++)
        out[i] *= 1.0f / 255.0f;
}

/* a convex polygon of n corners on the screen (a fan) to the backend (not
 * counted in tris_drawn: the effects are not triangles on the ARM) */
static void emit_fan(r3d_t *r, const r3d_corner_t *q, int n, int kind, const g16_sheet_t *tex)
{
    for (int i = 1; i + 1 < n; i++) {
        const r3d_corner_t t[3] = { q[0], q[i], q[i + 1] };
        r->backend->tri(r->backend->ctx, r->g, t, kind, tex, R3D_DEPTH_TEST);
    }
}

uint32_t r3d_point(r3d_t *r, v3_t p, float radius, uint32_t rgb, unsigned flags)
{
    view_t v;
    view_setup(r, &v);
    cv_t c;
    if (!to_camera(r, &v, p, &c))
        return 0;
    sv_t s = project(&v, c, 1.0f);
    g16_t *g = r->g;
    float rad = radius * v.f / c.z;
    if (rad < 0.5f) rad = 0.5f;
    if (r->backend) {
        /* tested against the depth and not writing it: an octagon, or for a
         * point of a few pixels a square of the same area (2 triangles, not
         * 6: the ARM's work is per triangle) */
        float col[3];
        fog_unit(r, rgb, c.z, col);
        r3d_corner_t q[8];
        const int kind = flags & R3D_FX_SCREEN ? R3D_KIND_SCREEN : R3D_KIND_COLOUR;
        if (rad < 3.0f) {
            const float h = rad * 0.886f;       /* sqrt(pi) / 2 */
            corner(&q[0], s.x - h, s.y - h, s.z, col[0], col[1], col[2]);
            corner(&q[1], s.x + h, s.y - h, s.z, col[0], col[1], col[2]);
            corner(&q[2], s.x + h, s.y + h, s.z, col[0], col[1], col[2]);
            corner(&q[3], s.x - h, s.y + h, s.z, col[0], col[1], col[2]);
            emit_fan(r, q, 4, kind, NULL);
        } else {
            for (int i = 0; i < 8; i++) {
                static const float cs[8][2] = { { 1, 0 }, { 0.7071f, 0.7071f }, { 0, 1 }, { -0.7071f, 0.7071f },
                                                { -1, 0 }, { -0.7071f, -0.7071f }, { 0, -1 }, { 0.7071f, -0.7071f } };
                corner(&q[i], s.x + cs[i][0] * rad, s.y + cs[i][1] * rad, s.z, col[0], col[1], col[2]);
            }
            emit_fan(r, q, 8, kind, NULL);
        }
        return (uint32_t)(rad * rad * 3.1f);
    }
    int x0 = (int)(s.x - rad), x1 = (int)(s.x + rad) + 1, y0 = (int)(s.y - rad), y1 = (int)(s.y + rad) + 1;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    int32_t zz = (int32_t)(s.z * 65535.0f);
    if (zz > 65535) zz = 65535;
    const uint16_t col = fog_colour(r, rgb, c.z);
    const float r2 = rad * rad;
    uint32_t n = 0;
    for (int y = y0; y < y1; y++) {
        float dy = y + 0.5f - s.y;
        uint16_t *row = g->px + (uint32_t)y * g->stride, *zrow = r->zbuf + (uint32_t)y * g->w;
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - s.x;
            if (dx * dx + dy * dy > r2 || zz <= zrow[x])
                continue;
            if ((flags & R3D_FX_SCREEN) && ((x + y) & 1))
                continue;
            row[x] = col;
            n++;
        }
    }
    r->pixels += n;
    return n;
}

uint32_t r3d_line(r3d_t *r, v3_t a, v3_t b, uint32_t rgb, int width, unsigned flags)
{
    view_t v;
    view_setup(r, &v);
    cv_t ca, cb;
    int ia = to_camera(r, &v, a, &ca), ib = to_camera(r, &v, b, &cb);
    if (!ia && !ib)
        return 0;
    if (!ia || !ib) {                       /* cut at the near plane */
        cv_t *in = ia ? &ca : &cb, *out = ia ? &cb : &ca;
        float t = (NEAR - in->z) / (out->z - in->z);
        *out = (cv_t){ in->x + (out->x - in->x) * t, in->y + (out->y - in->y) * t, NEAR, 0, 0, 0, 0, 0 };
    }
    sv_t sa = project(&v, ca, 1.0f), sb = project(&v, cb, 1.0f);
    g16_t *g = r->g;
    if (width < 1) width = 1;
    if (width > 8) width = 8;
    if (r->backend) {
        /* a band `width` pixels wide along the line */
        float col[3];
        fog_unit(r, rgb, (ca.z + cb.z) * 0.5f, col);
        float dx = sb.x - sa.x, dy = sb.y - sa.y, len = sqrtf(dx * dx + dy * dy);
        if (len < 0.5f) {
            dx = 0.5f; dy = 0; len = 0.5f;
        }
        const float k = 0.5f * (float)width / len, nx = -dy * k, ny = dx * k;
        r3d_corner_t q[4];
        corner(&q[0], sa.x + nx, sa.y + ny, sa.z, col[0], col[1], col[2]);
        corner(&q[1], sb.x + nx, sb.y + ny, sb.z, col[0], col[1], col[2]);
        corner(&q[2], sb.x - nx, sb.y - ny, sb.z, col[0], col[1], col[2]);
        corner(&q[3], sa.x - nx, sa.y - ny, sa.z, col[0], col[1], col[2]);
        emit_fan(r, q, 4, flags & R3D_FX_SCREEN ? R3D_KIND_SCREEN : R3D_KIND_COLOUR, NULL);
        return (uint32_t)(len * (float)width);
    }
    const uint16_t col = fog_colour(r, rgb, (ca.z + cb.z) * 0.5f);
    float dx = sb.x - sa.x, dy = sb.y - sa.y;
    float len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    int steps = (int)len + 1;
    if (steps > 4096) steps = 4096;
    float sx = dx / steps, sy = dy / steps, sz = (sb.z - sa.z) / steps;
    const int horiz = fabsf(dx) > fabsf(dy);
    uint32_t n = 0;
    float x = sa.x, y = sa.y, z = sa.z;
    for (int i = 0; i <= steps; i++, x += sx, y += sy, z += sz) {
        int32_t zz = (int32_t)(z * 65535.0f);
        if (zz > 65535) zz = 65535;
        for (int w = 0; w < width; w++) {
            int px = (int)x + (horiz ? 0 : w - width / 2), py = (int)y + (horiz ? w - width / 2 : 0);
            if (px < g->cx0 || px >= g->cx1 || py < g->cy0 || py >= g->cy1)
                continue;
            if ((flags & R3D_FX_SCREEN) && ((px + py) & 1))
                continue;
            uint16_t *zp = r->zbuf + (uint32_t)py * g->w + px;
            if (zz <= *zp)
                continue;
            g->px[(uint32_t)py * g->stride + px] = col;
            n++;
        }
    }
    r->pixels += n;
    return n;
}

uint32_t r3d_sprite(r3d_t *r, const g16_sheet_t *s, int sx, int sy, int sw, int sh, v3_t p,
                    float size, unsigned flags)
{
    if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0 || sx + sw > s->w || sy + sh > s->h)
        return 0;
    view_t v;
    view_setup(r, &v);
    cv_t c;
    if (!to_camera(r, &v, p, &c))
        return 0;
    sv_t sc = project(&v, c, 1.0f);
    g16_t *g = r->g;
    float w = size * v.f / c.z, h = w * (float)sh / (float)sw;
    if (w < 1) return 0;
    if (r->backend) {
        /* a quad of the sheet, transparent texels thrown away (R3D_FX_SCREEN
         * is not done: every pixel) */
        const float X0 = sc.x - w * 0.5f, Y0 = sc.y - h * 0.5f, X1 = sc.x + w * 0.5f, Y1 = sc.y + h * 0.5f;
        r3d_corner_t q[4];
        corner(&q[0], X0, Y0, sc.z, (float)sx, (float)sy, 1);
        corner(&q[1], X1, Y0, sc.z, (float)(sx + sw), (float)sy, 1);
        corner(&q[2], X1, Y1, sc.z, (float)(sx + sw), (float)(sy + sh), 1);
        corner(&q[3], X0, Y1, sc.z, (float)sx, (float)(sy + sh), 1);
        emit_fan(r, q, 4, R3D_KIND_TEXTURE, s);
        return (uint32_t)(w * h);
    }
    int x0 = (int)(sc.x - w * 0.5f), y0 = (int)(sc.y - h * 0.5f), x1 = (int)(sc.x + w * 0.5f), y1 = (int)(sc.y + h * 0.5f);
    const float ku = sw / w, kv = sh / h;
    int cx0 = x0 < g->cx0 ? g->cx0 : x0, cy0 = y0 < g->cy0 ? g->cy0 : y0;
    int cx1 = x1 > g->cx1 ? g->cx1 : x1, cy1 = y1 > g->cy1 ? g->cy1 : y1;
    int32_t zz = (int32_t)(sc.z * 65535.0f);
    if (zz > 65535) zz = 65535;
    uint32_t n = 0;
    for (int y = cy0; y < cy1; y++) {
        int ty = sy + (int)((y - y0 + 0.5f) * kv);
        if (ty >= sy + sh) ty = sy + sh - 1;
        uint16_t *row = g->px + (uint32_t)y * g->stride, *zrow = r->zbuf + (uint32_t)y * g->w;
        const uint16_t *trow = s->px + (uint32_t)ty * s->w;
        const uint8_t *arow = s->alpha + (uint32_t)ty * s->w;
        for (int x = cx0; x < cx1; x++) {
            int tx = sx + (int)((x - x0 + 0.5f) * ku);
            if (tx >= sx + sw) tx = sx + sw - 1;
            if (!arow[tx] || zz <= zrow[x])
                continue;
            if ((flags & R3D_FX_SCREEN) && ((x + y) & 1))
                continue;
            row[x] = trow[tx];
            n++;
        }
    }
    r->pixels += n;
    return n;
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
    m->vlod = calloc((size_t)nverts, 1);
    if (!m->verts || !m->faces || !m->colors || !m->normals || !m->vnormals || !m->vlod) {
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
    free(m->vlod); free(m->clight);
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
    /* the levels of detail that need each vertex */
    if (m->vlod) {
        memset(m->vlod, 0, (size_t)m->nverts);
        for (int t = 0; t < m->nfaces; t++) {
            uint8_t bits = 0;
            for (unsigned d = 0; d < 4; d++)
                if (R3D_LOD_SHOWS(m->colors[t], d))
                    bits |= (uint8_t)(1u << d);
            for (int k = 0; k < 3; k++)
                m->vlod[m->faces[t * 3 + k]] |= bits;
        }
    }
    /* vertex normals for Gouraud: the average of the faces that share the
     * vertex (weighted by their area: n before normalizing is 2x area);
     * flat faces do not count (they keep their own normal) */
    if (!m->vnormals)
        return;
    memset(m->vnormals, 0, (size_t)m->nverts * sizeof *m->vnormals);
    for (int t = 0; t < m->nfaces; t++) {
        if ((m->colors[t] & R3D_FLAT) && !(m->colors[t] & R3D_TEXTURED))
            continue;
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
