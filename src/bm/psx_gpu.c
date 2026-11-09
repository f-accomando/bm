/*
 * psx_gpu.c: the GPU of the PlayStation, in software. The GP0 drawing commands (polygons flat, Gouraud and
 * textured with 4, 8 and 15 bit textures; lines and polylines; rectangles and sprites; fills and the VRAM copies in
 * and out), the GP1 control commands, and the picture read from the 1024 x 512 VRAM. Semi-transparency, the
 * texture window, the mask bit and the dither are done. Original code.
 */
#include "psx_int.h"

#include <stdlib.h>

gpu_t GP;
static uint16_t pic[PSX_W_MAX * PSX_H_MAX];
int psx_gpu_line, psx_gpu_field;      /* set by the frame loop (psx_bus.c): the scanline and the field */

#define VR(x, y) GP.vram[(((y) & 511) << 10) | ((x) & 1023)]

typedef struct { int x, y, r, g, b, u, v; } vtx_t;

void psx_gpu_reset(void)
{
    GP.texpage = 0;
    GP.stat = 0;
    GP.tw_mx = GP.tw_my = GP.tw_ox = GP.tw_oy = 0;
    GP.da_x1 = GP.da_y1 = 0; GP.da_x2 = 1023; GP.da_y2 = 511;
    GP.off_x = GP.off_y = 0;
    GP.disp_x = GP.disp_y = 0;
    GP.hr_x1 = 0x200; GP.hr_x2 = 0x200 + 256 * 10;
    GP.vr_y1 = 0x10; GP.vr_y2 = 0x100;
    GP.dispmode = 0;
    GP.disp_off = 1;
    GP.dma_dir = 0;
    GP.irq = 0;
    GP.cmd_n = 0; GP.cmd_need = 0; GP.poly_line = 0; GP.xfer = 0;
}

/* ------------------------------------------------------------------ pixels */
static const int8_t dith[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };

static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* writes a pixel of colour r g b (0..255); semi: blend with the pixel there (mode from the texture page); bit15: the stp bit of a texel */
static inline void put(int x, int y, int r, int g, int b, int semi, int bit15, int dither)
{
    if (x < GP.da_x1 || x > GP.da_x2 || y < GP.da_y1 || y > GP.da_y2) return;
    uint16_t *d = &VR(x, y);
    const uint16_t back = *d;
    if ((GP.stat & 0x1000u) && (back & 0x8000u)) return;               /* check mask */
    if (dither) {
        const int dv = dith[y & 3][x & 3];
        r = clamp255(r + dv); g = clamp255(g + dv); b = clamp255(b + dv);
    }
    int fr = r >> 3, fg = g >> 3, fb = b >> 3;
    if (semi) {
        const int br = back & 31, bg = (back >> 5) & 31, bb = (back >> 10) & 31;
        switch ((GP.texpage >> 5) & 3) {
        case 0: fr = (br + fr) >> 1; fg = (bg + fg) >> 1; fb = (bb + fb) >> 1; break;
        case 1: fr = br + fr; fg = bg + fg; fb = bb + fb; break;
        case 2: fr = br - fr; fg = bg - fg; fb = bb - fb; break;
        default: fr = br + (fr >> 2); fg = bg + (fg >> 2); fb = bb + (fb >> 2); break;
        }
        fr = fr < 0 ? 0 : fr > 31 ? 31 : fr;
        fg = fg < 0 ? 0 : fg > 31 ? 31 : fg;
        fb = fb < 0 ? 0 : fb > 31 ? 31 : fb;
    }
    *d = (uint16_t)(fr | fg << 5 | fb << 10 | ((GP.stat & 0x800u) ? 0x8000 : (bit15 ? 0x8000 : 0)));
}

/* ------------------------------------------------------------------ textures */
typedef struct {
    int depth, xb, yb, cx, cy;
    int win, uand, uor, vand, vor;      /* the texture window as masks: u = (u & uand) | uor */
    const uint16_t *clut;               /* the VRAM row of the CLUT */
} tex_t;

static void tex_setup(tex_t *t, uint32_t clut, uint32_t tp)
{
    t->xb = (int)(tp & 15) * 64;
    t->yb = (int)((tp >> 4) & 1) * 256;
    t->depth = (int)((tp >> 7) & 3);
    t->cx = (int)(clut & 0x3F) * 16;
    t->cy = (int)((clut >> 6) & 0x1FF);
    t->win = (GP.tw_mx | GP.tw_my) != 0;
    t->uand = ~(GP.tw_mx * 8); t->uor = (GP.tw_ox & GP.tw_mx) * 8;
    t->vand = ~(GP.tw_my * 8); t->vor = (GP.tw_oy & GP.tw_my) * 8;
    t->clut = &GP.vram[(size_t)t->cy << 10];
}

/* 0 for a transparent texel, else the 15 bit colour with bit 15 set if the stp bit is; depth is a constant in the callers */
static inline __attribute__((always_inline)) uint16_t texel_d(const tex_t *t, int u, int v, const int depth)
{
    if (t->win) { u = (u & t->uand) | t->uor; v = (v & t->vand) | t->vor; }
    u &= 255; v &= 255;
    const uint16_t *row = &GP.vram[(size_t)(((t->yb + v) & 511) << 10)];
    if (depth == 0) {
        const uint16_t w = row[(t->xb + (u >> 2)) & 1023];
        return t->clut[(t->cx + ((w >> ((u & 3) * 4)) & 15)) & 1023];
    }
    if (depth == 1) {
        const uint16_t w = row[(t->xb + (u >> 1)) & 1023];
        return t->clut[(t->cx + ((w >> ((u & 1) * 8)) & 255)) & 1023];
    }
    return row[(t->xb + u) & 1023];
}

/* what a primitive needs to know to write its pixels */
typedef struct { int semi, raw, dither, simple, abr; } prim_t;

static void prim_setup(prim_t *P, int semi, int raw, int dither)
{
    P->semi = semi; P->raw = raw; P->dither = dither;
    P->abr = (int)((GP.texpage >> 5) & 3);
    P->simple = !(GP.stat & 0x1800u) && !dither;          /* no mask test, no forced mask bit, no dither: the pixel is written as is */
}

/* one row of a polygon or a sprite. TEX, GOU and DEPTH are constants in the callers, so each combination is its own loop.
 * cur: the colour (0..2) and texture (3, 4) accumulators in 16.16; dxs their steps per pixel. */
static inline __attribute__((always_inline)) void span_t(uint16_t *rowp, int y, int xl, int xr, uint32_t *cur, const int32_t *dxs,
                                                         int fr, int fg, int fb, const tex_t *t, const prim_t *P,
                                                         const int TEX, const int GOU, const int DEPTH)
{
    for (int x = xl; x <= xr; x++) {
        int r = fr, g = fg, b = fb, bit15 = 0, semi = P->semi, dith = P->dither;
        if (GOU) { r = clamp255((int)cur[0] >> 16); g = clamp255((int)cur[1] >> 16); b = clamp255((int)cur[2] >> 16); }
        if (TEX) {
            const uint16_t tx = texel_d(t, (int)(cur[3] >> 16), (int)(cur[4] >> 16), DEPTH);
            if (tx == 0) goto next;
            int tr = (tx & 31) << 3, tg = ((tx >> 5) & 31) << 3, tb = ((tx >> 10) & 31) << 3;
            if (!P->raw) { tr = clamp255((tr * r) >> 7); tg = clamp255((tg * g) >> 7); tb = clamp255((tb * b) >> 7); }
            else dith = 0;
            r = tr; g = tg; b = tb;
            semi = semi && (tx & 0x8000);
            bit15 = tx >> 15;
        }
        if (P->simple) {
            int pr = r >> 3, pg = g >> 3, pb = b >> 3;
            if (semi) {
                const uint16_t back = rowp[x];
                const int br = back & 31, bg = (back >> 5) & 31, bb = (back >> 10) & 31;
                switch (P->abr) {
                case 0: pr = (br + pr) >> 1; pg = (bg + pg) >> 1; pb = (bb + pb) >> 1; break;
                case 1: pr = br + pr; pg = bg + pg; pb = bb + pb; break;
                case 2: pr = br - pr; pg = bg - pg; pb = bb - pb; break;
                default: pr = br + (pr >> 2); pg = bg + (pg >> 2); pb = bb + (pb >> 2); break;
                }
                pr = pr < 0 ? 0 : pr > 31 ? 31 : pr;
                pg = pg < 0 ? 0 : pg > 31 ? 31 : pg;
                pb = pb < 0 ? 0 : pb > 31 ? 31 : pb;
            }
            rowp[x] = (uint16_t)(pr | pg << 5 | pb << 10 | (bit15 << 15));
        } else {
            put(x, y, r, g, b, semi, bit15, dith);
        }
    next:
        if (GOU) { cur[0] += (uint32_t)dxs[0]; cur[1] += (uint32_t)dxs[1]; cur[2] += (uint32_t)dxs[2]; }
        if (TEX) { cur[3] += (uint32_t)dxs[3]; cur[4] += (uint32_t)dxs[4]; }
    }
}

static void run_span(uint16_t *rowp, int y, int xl, int xr, uint32_t *cur, const int32_t *dxs, int fr, int fg, int fb,
                     const tex_t *t, const prim_t *P, int gouraud)
{
    if (!t) {
        if (gouraud) span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 0, 1, 0);
        else span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 0, 0, 0);
    } else if (t->depth == 0) {
        if (gouraud) span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 1, 1, 0);
        else span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 1, 0, 0);
    } else if (t->depth == 1) {
        if (gouraud) span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 1, 1, 1);
        else span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 1, 0, 1);
    } else {
        if (gouraud) span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 1, 1, 2);
        else span_t(rowp, y, xl, xr, cur, dxs, fr, fg, fb, t, P, 1, 0, 2);
    }
}

/* ------------------------------------------------------------------ triangles */
static void tri(vtx_t a, vtx_t b, vtx_t c, int gouraud, const tex_t *t, int semi, int raw)
{
    int64_t area = (int64_t)(b.x - a.x) * (c.y - a.y) - (int64_t)(b.y - a.y) * (c.x - a.x);
    if (area == 0) return;
    if (area < 0) { vtx_t s = b; b = c; c = s; area = -area; }
    if (abs(a.x - b.x) > 1023 || abs(b.x - c.x) > 1023 || abs(a.x - c.x) > 1023 ||
        abs(a.y - b.y) > 511 || abs(b.y - c.y) > 511 || abs(a.y - c.y) > 511) return;
    int minx = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    int maxx = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    int miny = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    int maxy = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
    if (minx < GP.da_x1) minx = GP.da_x1;
    if (maxx > GP.da_x2) maxx = GP.da_x2;
    if (miny < GP.da_y1) miny = GP.da_y1;
    if (maxy > GP.da_y2) maxy = GP.da_y2;
    if (minx > maxx || miny > maxy) return;
    /* edge functions: w0 for a (edge b-c), w1 for b (c-a), w2 for c (a-b); inside when w >= bias */
    const vtx_t *e0[3] = { &b, &c, &a }, *e1[3] = { &c, &a, &b };
    int64_t A[3], B[3], thr[3];
    for (int k = 0; k < 3; k++) {
        const int dx = e1[k]->x - e0[k]->x, dy = e1[k]->y - e0[k]->y;
        A[k] = -dy;                                       /* per x */
        B[k] = dx;                                        /* per y */
        thr[k] = (dy < 0 || (dy == 0 && dx > 0)) ? 0 : 1; /* top-left rule */
    }
    /* gradients (16.16) of the attributes */
    int32_t dx_[5] = { 0 }, dy_[5] = { 0 };
    int32_t base[5] = { a.r, a.g, a.b, a.u, a.v };
    const int nat = t ? 5 : (gouraud ? 3 : 0);
    int32_t va[5] = { a.r, a.g, a.b, a.u, a.v }, vb[5] = { b.r, b.g, b.b, b.u, b.v }, vc[5] = { c.r, c.g, c.b, c.u, c.v };
    for (int k = 0; k < nat; k++) {
        const int64_t nx = (int64_t)(vb[k] - va[k]) * (c.y - a.y) - (int64_t)(vc[k] - va[k]) * (b.y - a.y);
        const int64_t ny = (int64_t)(vc[k] - va[k]) * (b.x - a.x) - (int64_t)(vb[k] - va[k]) * (c.x - a.x);
        dx_[k] = (int32_t)((nx * 65536) / area);
        dy_[k] = (int32_t)((ny * 65536) / area);
    }
    if (!gouraud) { base[0] = a.r; base[1] = a.g; base[2] = a.b; }
    const int dither = (GP.texpage & 0x200) && (gouraud || (t && !raw));
    prim_t P;
    prim_setup(&P, semi, raw, dither);
    for (int y = miny; y <= maxy; y++) {
        /* the x range of the row from the three edges */
        int xl = minx, xr = maxx;
        for (int k = 0; k < 3; k++) {
            const int64_t w = A[k] * (minx - e0[k]->x) + B[k] * (y - e0[k]->y) - thr[k];   /* w at minx, minus the threshold */
            if (A[k] == 0) { if (w < 0) { xl = 1; xr = 0; break; } }
            else if (A[k] > 0) { const int64_t n = (-w + A[k] - 1) / A[k]; if (w < 0 && minx + n > xl) xl = (int)(minx + n); }
            else { const int64_t s = -A[k]; const int64_t n = w / s; if (w < 0) { xl = 1; xr = 0; break; } if (minx + n < xr) xr = (int)(minx + n); }
        }
        if (xl > xr) continue;
        uint32_t cur[5];
        for (int k = 0; k < nat; k++)
            cur[k] = ((uint32_t)base[k] << 16) + (k < 3 ? 0x8000u : 0u) + (uint32_t)dx_[k] * (uint32_t)(xl - a.x) + (uint32_t)dy_[k] * (uint32_t)(y - a.y);
        run_span(&GP.vram[(size_t)y << 10], y, xl, xr, cur, dx_, a.r, a.g, a.b, t, &P, gouraud);
    }
    GP.pixels += (uint32_t)(maxx - minx + 1) * (uint32_t)(maxy - miny + 1) / 2;
}

/* ------------------------------------------------------------------ lines */
static void line(vtx_t a, vtx_t b, int gouraud, int semi)
{
    int dx = abs(b.x - a.x), dy = abs(b.y - a.y);
    if (dx > 1023 || dy > 511) return;
    const int n = dx > dy ? dx : dy;
    const int dither = (GP.texpage & 0x200) && gouraud;
    for (int i = 0; i <= n; i++) {
        const int x = n ? a.x + (b.x - a.x) * i / n : a.x, y = n ? a.y + (b.y - a.y) * i / n : a.y;
        int r = a.r, g = a.g, bl = a.b;
        if (gouraud && n) { r = a.r + (b.r - a.r) * i / n; g = a.g + (b.g - a.g) * i / n; bl = a.b + (b.b - a.b) * i / n; }
        put(x, y, r, g, bl, semi, 0, dither);
    }
}

/* ------------------------------------------------------------------ the commands */
static inline int sx(uint32_t w) { return ((int)(w << 21)) >> 21; }
static inline int sy(uint32_t w) { return ((int)(w >> 16 << 21)) >> 21; }

static void cmd_polygon(void)
{
    const uint32_t c0 = GP.cmd[0];
    const int op = (int)(c0 >> 24);
    const int gouraud = (op & 0x10) != 0, quad = (op & 8) != 0, textured = (op & 4) != 0, semi = (op & 2) != 0, raw = (op & 1) != 0;
    const int n = quad ? 4 : 3;
    vtx_t v[4];
    int w = 1;
    uint32_t clut = 0, tp = 0;
    for (int i = 0; i < n; i++) {
        uint32_t col = c0;
        if (i > 0 && gouraud) col = GP.cmd[w++];
        v[i].r = (int)(col & 255); v[i].g = (int)((col >> 8) & 255); v[i].b = (int)((col >> 16) & 255);
        const uint32_t xy = GP.cmd[w++];
        v[i].x = sx(xy) + GP.off_x; v[i].y = sy(xy) + GP.off_y;
        if (textured) {
            const uint32_t uv = GP.cmd[w++];
            v[i].u = (int)(uv & 255); v[i].v = (int)((uv >> 8) & 255);
            if (i == 0) clut = uv >> 16;
            if (i == 1) tp = uv >> 16;
        }
    }
    tex_t t;
    const tex_t *tp_ = NULL;
    if (textured) {
        GP.texpage = (GP.texpage & ~0x1FFu) | (tp & 0x1FF);
        GP.stat = (GP.stat & ~0x8000u) | ((tp & 0x800) ? 0x8000u : 0);
        tex_setup(&t, clut, GP.texpage);
        tp_ = &t;
    }
    if (textured && raw) { for (int i = 0; i < n; i++) v[i].r = v[i].g = v[i].b = 128; }
    tri(v[0], v[1], v[2], gouraud, tp_, semi, raw);
    if (quad) tri(v[1], v[2], v[3], gouraud, tp_, semi, raw);
}

static void cmd_line(void)
{
    const uint32_t c0 = GP.cmd[0];
    const int op = (int)(c0 >> 24), gouraud = (op & 0x10) != 0, semi = (op & 2) != 0;
    vtx_t a, b;
    a.r = (int)(c0 & 255); a.g = (int)((c0 >> 8) & 255); a.b = (int)((c0 >> 16) & 255);
    a.x = sx(GP.cmd[1]) + GP.off_x; a.y = sy(GP.cmd[1]) + GP.off_y;
    uint32_t c1 = gouraud ? GP.cmd[2] : c0;
    b.r = (int)(c1 & 255); b.g = (int)((c1 >> 8) & 255); b.b = (int)((c1 >> 16) & 255);
    const uint32_t xy = GP.cmd[gouraud ? 3 : 2];
    b.x = sx(xy) + GP.off_x; b.y = sy(xy) + GP.off_y;
    line(a, b, gouraud, semi);
    GP.line_colors[0] = c1;
    GP.line_xy[0] = (int32_t)xy;
}

static void cmd_rect(void)
{
    const uint32_t c0 = GP.cmd[0];
    const int op = (int)(c0 >> 24), textured = (op & 4) != 0, semi = (op & 2) != 0, raw = (op & 1) != 0, size = (op >> 3) & 3;
    int w = 1;
    const uint32_t xy = GP.cmd[w++];
    uint32_t uv = 0;
    if (textured) uv = GP.cmd[w++];
    int rw, rh;
    if (size == 0) { const uint32_t s = GP.cmd[w]; rw = (int)(s & 0x3FF); rh = (int)((s >> 16) & 0x1FF); }
    else { rw = rh = size == 1 ? 1 : size == 2 ? 8 : 16; }
    const int x0 = sx(xy) + GP.off_x, y0 = sy(xy) + GP.off_y;
    int r = (int)(c0 & 255), g = (int)((c0 >> 8) & 255), b = (int)((c0 >> 16) & 255);
    tex_t t;
    const tex_t *tp = NULL;
    if (textured) { tex_setup(&t, uv >> 16, GP.texpage); tp = &t; if (raw) r = g = b = 128; }
    const int u0 = (int)(uv & 255), v0 = (int)((uv >> 8) & 255);
    prim_t P;
    prim_setup(&P, semi, raw, 0);
    int xa = x0 < GP.da_x1 ? GP.da_x1 : x0, xb = x0 + rw - 1 > GP.da_x2 ? GP.da_x2 : x0 + rw - 1;
    const int32_t dxs[5] = { 0, 0, 0, 0x10000, 0 };
    for (int y = 0; y < rh && xa <= xb; y++) {
        const int py = y0 + y;
        if (py < GP.da_y1 || py > GP.da_y2) continue;
        uint32_t cur[5] = { 0, 0, 0, (uint32_t)(u0 + (xa - x0)) << 16, (uint32_t)((v0 + y) & 255) << 16 };
        run_span(&GP.vram[(size_t)py << 10], py, xa, xb, cur, dxs, r, g, b, tp, &P, 0);
    }
    GP.pixels += (uint32_t)rw * (uint32_t)rh;
}

static void cmd_fill(void)
{
    const uint32_t c = GP.cmd[0];
    const int x = (int)(GP.cmd[1] & 0x3F0), y = (int)((GP.cmd[1] >> 16) & 0x1FF);
    const int w = (int)(((GP.cmd[2] & 0x3FF) + 0xF) & ~0xFu), h = (int)((GP.cmd[2] >> 16) & 0x1FF);
    const uint16_t px = (uint16_t)(((c & 255) >> 3) | (((c >> 8) & 255) >> 3) << 5 | (((c >> 16) & 255) >> 3) << 10);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) VR(x + i, y + j) = px;
}

static void cmd_vram_copy(void)
{
    const int sxp = (int)(GP.cmd[1] & 0x3FF), syp = (int)((GP.cmd[1] >> 16) & 0x1FF);
    const int dxp = (int)(GP.cmd[2] & 0x3FF), dyp = (int)((GP.cmd[2] >> 16) & 0x1FF);
    int w = (int)(GP.cmd[3] & 0x3FF), h = (int)((GP.cmd[3] >> 16) & 0x1FF);
    if (!w) w = 1024;
    if (!h) h = 512;
    static uint16_t tmp[1024 * 512];
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) tmp[j * w + i] = VR(sxp + i, syp + j);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            uint16_t *d = &VR(dxp + i, dyp + j);
            if ((GP.stat & 0x1000u) && (*d & 0x8000u)) continue;
            *d = tmp[j * w + i] | ((GP.stat & 0x800u) ? 0x8000 : 0);
        }
}

uint32_t psx_gpu_us;

static void exec_cmd(void)
{
    const uint32_t c0 = GP.cmd[0];
    const int op = (int)(c0 >> 24);
    GP.cmds++;
    if (op >= 0x20 && op < 0x80) {                            /* the drawing commands are timed: the frame time splits into CPU and GPU */
        const uint32_t t0 = psx_now_us();
        if (op < 0x40) cmd_polygon(); else if (op < 0x60) cmd_line(); else cmd_rect();
        psx_gpu_us += psx_now_us() - t0;
        return;
    }
    if (op >= 0x80 && op < 0xA0) { cmd_vram_copy(); return; }
    if (op >= 0xA0 && op < 0xC0) {
        GP.xfer = 1;
        GP.xfer_x = (int)(GP.cmd[1] & 0x3FF); GP.xfer_y = (int)((GP.cmd[1] >> 16) & 0x1FF);
        GP.xfer_w = (int)(GP.cmd[2] & 0x3FF); GP.xfer_h = (int)((GP.cmd[2] >> 16) & 0x1FF);
        if (!GP.xfer_w) GP.xfer_w = 1024;
        if (!GP.xfer_h) GP.xfer_h = 512;
        GP.xfer_cx = GP.xfer_cy = 0;
        return;
    }
    if (op >= 0xC0 && op < 0xE0) {
        GP.xfer = 2;
        GP.xfer_x = (int)(GP.cmd[1] & 0x3FF); GP.xfer_y = (int)((GP.cmd[1] >> 16) & 0x1FF);
        GP.xfer_w = (int)(GP.cmd[2] & 0x3FF); GP.xfer_h = (int)((GP.cmd[2] >> 16) & 0x1FF);
        if (!GP.xfer_w) GP.xfer_w = 1024;
        if (!GP.xfer_h) GP.xfer_h = 512;
        GP.xfer_cx = GP.xfer_cy = 0;
        return;
    }
    switch (op) {
    case 0x01: break;
    case 0x02: cmd_fill(); break;
    case 0x1F: GP.irq = 1; psx_irq_raise(IRQ_GPU); break;
    case 0xE1:
        GP.texpage = c0 & 0x7FF;
        GP.stat = (GP.stat & ~0x8000u) | ((c0 & 0x800) ? 0x8000u : 0);
        break;
    case 0xE2:
        GP.tw_mx = (int)(c0 & 31); GP.tw_my = (int)((c0 >> 5) & 31);
        GP.tw_ox = (int)((c0 >> 10) & 31); GP.tw_oy = (int)((c0 >> 15) & 31);
        break;
    case 0xE3: GP.da_x1 = (int)(c0 & 0x3FF); GP.da_y1 = (int)((c0 >> 10) & 0x1FF); break;
    case 0xE4: GP.da_x2 = (int)(c0 & 0x3FF); GP.da_y2 = (int)((c0 >> 10) & 0x1FF); break;
    case 0xE5: GP.off_x = ((int)(c0 << 21)) >> 21; GP.off_y = ((int)((c0 >> 11) << 21)) >> 21; break;
    case 0xE6: GP.stat = (GP.stat & ~0x1800u) | ((c0 & 1) ? 0x800u : 0) | ((c0 & 2) ? 0x1000u : 0); break;
    default: break;
    }
}

/* the number of words of the command whose first word is w; polylines are one segment at a time */
static int cmd_len(uint32_t w)
{
    const int op = (int)(w >> 24);
    if (op >= 0x20 && op < 0x40) {
        const int n = (op & 8) ? 4 : 3;
        return 1 + n + ((op & 4) ? n : 0) + ((op & 0x10) ? n - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) return (op & 0x10) ? 4 : 3;
    if (op >= 0x60 && op < 0x80) return 2 + ((op & 4) ? 1 : 0) + (((op >> 3) & 3) == 0 ? 1 : 0);
    if (op >= 0x80 && op < 0xA0) return 4;
    if (op >= 0xA0 && op < 0xE0) return 3;
    if (op == 0x02) return 3;
    return 1;
}

void psx_gp0_write(uint32_t v)
{
    if (GP.xfer == 1) {                                    /* the CPU writes pixels */
        for (int k = 0; k < 2; k++) {
            if (GP.xfer_cy >= GP.xfer_h) break;
            const uint16_t px = (uint16_t)(v >> (16 * k));
            uint16_t *d = &VR(GP.xfer_x + GP.xfer_cx, GP.xfer_y + GP.xfer_cy);
            if (!((GP.stat & 0x1000u) && (*d & 0x8000u))) *d = px | ((GP.stat & 0x800u) ? 0x8000 : 0);
            if (++GP.xfer_cx >= GP.xfer_w) { GP.xfer_cx = 0; GP.xfer_cy++; }
        }
        if (GP.xfer_cy >= GP.xfer_h) GP.xfer = 0;
        return;
    }
    if (GP.poly_line) {
        if (GP.cmd_n == 0 && (v & 0xF000F000u) == 0x50005000u) { GP.poly_line = 0; return; }
        GP.cmd[GP.cmd_n++] = v;
        if (GP.cmd_n >= GP.cmd_need) {
            /* a segment from the last point (line_xy[0]) to the new one */
            const int gouraud = (GP.poly_op >> 24) & 0x10, semi = ((GP.poly_op >> 24) & 2) != 0;
            vtx_t a, b;
            const uint32_t ca = GP.line_colors[0], cb = gouraud ? GP.cmd[0] : ca;
            const uint32_t xyb = gouraud ? GP.cmd[1] : GP.cmd[0];
            a.r = (int)(ca & 255); a.g = (int)((ca >> 8) & 255); a.b = (int)((ca >> 16) & 255);
            a.x = sx((uint32_t)GP.line_xy[0]) + GP.off_x; a.y = sy((uint32_t)GP.line_xy[0]) + GP.off_y;
            b.r = (int)(cb & 255); b.g = (int)((cb >> 8) & 255); b.b = (int)((cb >> 16) & 255);
            b.x = sx(xyb) + GP.off_x; b.y = sy(xyb) + GP.off_y;
            line(a, b, gouraud != 0, semi);
            GP.line_colors[0] = cb;
            GP.line_xy[0] = (int32_t)xyb;
            GP.cmd_n = 0;
        }
        return;
    }
    if (GP.cmd_n == 0)
        GP.cmd_need = cmd_len(v);
    GP.cmd[GP.cmd_n++] = v;
    if (GP.cmd_n < GP.cmd_need) return;
    GP.cmd_n = 0;
    exec_cmd();
    const int op = (int)(GP.cmd[0] >> 24);
    if (op >= 0x40 && op < 0x60 && (op & 8)) {            /* a polyline: one segment is drawn, more may follow */
        GP.poly_line = 1;
        GP.poly_op = GP.cmd[0];
        GP.cmd_need = (op & 0x10) ? 2 : 1;
    }
}

uint32_t psx_gpu_read(void)
{
    if (GP.xfer == 2) {
        uint32_t w = 0;
        for (int k = 0; k < 2; k++) {
            if (GP.xfer_cy < GP.xfer_h)
                w |= (uint32_t)VR(GP.xfer_x + GP.xfer_cx, GP.xfer_y + GP.xfer_cy) << (16 * k);
            if (++GP.xfer_cx >= GP.xfer_w) { GP.xfer_cx = 0; GP.xfer_cy++; }
        }
        if (GP.xfer_cy >= GP.xfer_h) GP.xfer = 0;
        GP.read_latch = w;
    }
    return GP.read_latch;
}

void psx_gp1_write(uint32_t v)
{
    const uint32_t cmd = v >> 24;
    switch (cmd) {
    case 0x00: psx_gpu_reset(); break;
    case 0x01: GP.cmd_n = 0; GP.poly_line = 0; GP.xfer = 0; break;
    case 0x02: GP.irq = 0; break;
    case 0x03: GP.disp_off = v & 1; break;
    case 0x04: GP.dma_dir = (int)(v & 3); break;
    case 0x05: GP.disp_x = (int)(v & 0x3FF); GP.disp_y = (int)((v >> 10) & 0x1FF); break;
    case 0x06: GP.hr_x1 = (int)(v & 0xFFF); GP.hr_x2 = (int)((v >> 12) & 0xFFF); break;
    case 0x07: GP.vr_y1 = (int)(v & 0x3FF); GP.vr_y2 = (int)((v >> 10) & 0x3FF); break;
    case 0x08: GP.dispmode = v & 0xFF; break;
    default:
        if (cmd >= 0x10 && cmd < 0x20) {
            switch (cmd & 15) {
            case 2: GP.read_latch = (uint32_t)(GP.tw_mx | GP.tw_my << 5 | GP.tw_ox << 10 | GP.tw_oy << 15); break;
            case 3: GP.read_latch = (uint32_t)(GP.da_x1 | GP.da_y1 << 10); break;
            case 4: GP.read_latch = (uint32_t)(GP.da_x2 | GP.da_y2 << 10); break;
            case 5: GP.read_latch = (uint32_t)((GP.off_x & 0x7FF) | (GP.off_y & 0x7FF) << 11); break;
            case 7: GP.read_latch = 2; break;
            default: break;
            }
        }
        break;
    }
}

uint32_t psx_gpu_stat(void)
{
    uint32_t s = (GP.texpage & 0x7FF) | GP.stat;
    s |= 1u << 13;
    s |= (GP.dispmode & 3) << 17;
    s |= ((GP.dispmode >> 2) & 1) << 19;
    s |= ((GP.dispmode >> 3) & 1) << 20;
    s |= ((GP.dispmode >> 4) & 1) << 21;
    s |= ((GP.dispmode >> 5) & 1) << 22;
    s |= ((GP.dispmode >> 6) & 1) << 16;
    s |= (uint32_t)(GP.disp_off ? 1 : 0) << 23;
    s |= (uint32_t)GP.irq << 24;
    s |= 7u << 26;                                         /* ready for a command, to send VRAM, to receive DMA */
    s |= (uint32_t)GP.dma_dir << 29;
    switch (GP.dma_dir) {
    case 1: s |= 1u << 25; break;
    case 2: s |= 1u << 25; break;
    case 3: s |= 1u << 25; break;
    default: break;
    }
    /* bit 31: the field in 480 line interlaced mode, else the parity of the scanline */
    if ((GP.dispmode & 0x24) == 0x24 ? psx_gpu_field : (psx_gpu_line & 1)) s |= 1u << 31;
    return s;
}

void psx_gpu_dma_to(const uint32_t *words, int n)
{
    for (int i = 0; i < n; i++) psx_gp0_write(words[i]);
}

int psx_gpu_dma_from(uint32_t *words, int n)
{
    for (int i = 0; i < n; i++) words[i] = psx_gpu_read();
    return n;
}

int psx_gpu_hres(void)
{
    if (GP.dispmode & 0x40) return 368;
    static const int w[4] = { 256, 320, 512, 640 };
    return w[GP.dispmode & 3];
}

const uint16_t *psx_gpu_picture(int *pw, int *ph)
{
    const int w = psx_gpu_hres();
    int h = GP.vr_y2 - GP.vr_y1;
    if (h < 16 || h > 288) h = 240;
    const int tall = (GP.dispmode & 0x24) == 0x24;
    if (tall) h *= 2;
    if (h > PSX_H_MAX) h = PSX_H_MAX;
    if (GP.disp_off) {
        memset(pic, 0, (size_t)w * (size_t)h * 2);
    } else if (GP.dispmode & 0x10) {                       /* 24 bits per pixel */
        for (int y = 0; y < h; y++) {
            const int vy = GP.disp_y + (tall ? y : y);
            int vx = GP.disp_x * 2;                        /* in bytes */
            for (int x = 0; x < w; x++) {
                const uint8_t *row = (const uint8_t *)&GP.vram[(vy & 511) << 10];
                const int o = (vx + x * 3) & 2047;
                const int r = row[o], g = row[(o + 1) & 2047], b = row[(o + 2) & 2047];
                pic[y * w + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
            }
        }
    } else {
        for (int y = 0; y < h; y++) {
            const uint16_t *row = &GP.vram[((GP.disp_y + y) & 511) << 10];
            uint16_t *o = &pic[y * w];
            for (int x = 0; x < w; x++) {
                const uint16_t p = row[(GP.disp_x + x) & 1023];
                o[x] = (uint16_t)(((p & 31) << 11) | (((p >> 5) & 31) << 6) | ((p >> 10) & 31));
            }
        }
    }
    *pw = w;
    *ph = h;
    return pic;
}
