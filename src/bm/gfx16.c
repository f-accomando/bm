#include "gfx16.h"

#include <stdlib.h>
#include <string.h>

uint32_t g16_to_rgb24(uint16_t c)
{
    uint32_t r = c >> 11, g = c >> 5 & 0x3F, b = c & 0x1F;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

void g16_target(g16_t *g, uint16_t *px, uint32_t stride, int w, int h, const font_t *font)
{
    g->px = px;
    g->stride = stride;
    g->w = w;
    g->h = h;
    g->font = font;
    g->cam_x = g->cam_y = 0;
    g16_clip(g, 0, 0, 0, 0);
}

void g16_clip(g16_t *g, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        g->cx0 = 0; g->cy0 = 0; g->cx1 = g->w; g->cy1 = g->h;
        return;
    }
    g->cx0 = x < 0 ? 0 : x;
    g->cy0 = y < 0 ? 0 : y;
    g->cx1 = x + w > g->w ? g->w : x + w;
    g->cy1 = y + h > g->h ? g->h : y + h;
    if (g->cx1 < g->cx0) g->cx1 = g->cx0;
    if (g->cy1 < g->cy0) g->cy1 = g->cy0;
}

void g16_camera(g16_t *g, int x, int y)
{
    g->cam_x = x;
    g->cam_y = y;
}

/* Horizontal span in screen coordinates, already clipped by the caller. */
static inline void span(uint16_t *p, int n, uint16_t c)
{
    if (n > 0 && ((uintptr_t)p & 2)) { *p++ = c; n--; }
    uint32_t c2 = (uint32_t)c << 16 | c, *q = (uint32_t *)p;
    while (n >= 8) { q[0] = c2; q[1] = c2; q[2] = c2; q[3] = c2; q += 4; n -= 8; }
    while (n >= 2) { *q++ = c2; n -= 2; }
    if (n) *(uint16_t *)q = c;
}

void g16_cls(g16_t *g, uint16_t c)
{
    for (int y = 0; y < g->h; y++)
        span(g->px + (uint32_t)y * g->stride, g->w, c);
}

static inline void hline(g16_t *g, int x0, int x1, int y, uint16_t c)   /* screen coords, inclusive */
{
    if (y < g->cy0 || y >= g->cy1) return;
    if (x0 < g->cx0) x0 = g->cx0;
    if (x1 >= g->cx1) x1 = g->cx1 - 1;
    if (x0 > x1) return;
    span(g->px + (uint32_t)y * g->stride + x0, x1 - x0 + 1, c);
}

static inline void plot(g16_t *g, int x, int y, uint16_t c)             /* screen coords */
{
    if (x >= g->cx0 && x < g->cx1 && y >= g->cy0 && y < g->cy1)
        g->px[(uint32_t)y * g->stride + x] = c;
}

void g16_pset(g16_t *g, int x, int y, uint16_t c)
{
    plot(g, x - g->cam_x, y - g->cam_y, c);
}

int g16_pget(const g16_t *g, int x, int y)
{
    x -= g->cam_x;
    y -= g->cam_y;
    if (x < 0 || y < 0 || x >= g->w || y >= g->h)
        return -1;
    return g->px[(uint32_t)y * g->stride + x];
}

void g16_line(g16_t *g, int x0, int y0, int x1, int y1, uint16_t c)
{
    x0 -= g->cam_x; x1 -= g->cam_x; y0 -= g->cam_y; y1 -= g->cam_y;
    if (y0 == y1) {
        hline(g, x0 < x1 ? x0 : x1, x0 < x1 ? x1 : x0, y0, c);
        return;
    }
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(g, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void g16_rectfill(g16_t *g, int x, int y, int w, int h, uint16_t c)
{
    x -= g->cam_x;
    y -= g->cam_y;
    int x1 = x + w - 1, y1 = y + h - 1;
    if (y < g->cy0) y = g->cy0;
    if (y1 >= g->cy1) y1 = g->cy1 - 1;
    for (; y <= y1; y++)
        hline(g, x, x1, y, c);
}

void g16_rect(g16_t *g, int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    int sx = x - g->cam_x, sy = y - g->cam_y;
    hline(g, sx, sx + w - 1, sy, c);
    hline(g, sx, sx + w - 1, sy + h - 1, c);
    for (int yy = sy + 1; yy < sy + h - 1; yy++) {
        plot(g, sx, yy, c);
        plot(g, sx + w - 1, yy, c);
    }
}

void g16_circ(g16_t *g, int cx, int cy, int r, uint16_t c)
{
    cx -= g->cam_x;
    cy -= g->cam_y;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        plot(g, cx + x, cy + y, c); plot(g, cx - x, cy + y, c);
        plot(g, cx + x, cy - y, c); plot(g, cx - x, cy - y, c);
        plot(g, cx + y, cy + x, c); plot(g, cx - y, cy + x, c);
        plot(g, cx + y, cy - x, c); plot(g, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void g16_circfill(g16_t *g, int cx, int cy, int r, uint16_t c)
{
    cx -= g->cam_x;
    cy -= g->cam_y;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        hline(g, cx - x, cx + x, cy + y, c);
        hline(g, cx - x, cx + x, cy - y, c);
        hline(g, cx - y, cx + y, cy + x, c);
        hline(g, cx - y, cx + y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

/* Core blit: sheet rectangle -> screen position (screen coords), clipped. */
static void blit(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                 int dx, int dy, int flip_x, int flip_y, int opaque)
{
    int x0 = dx, y0 = dy, x1 = dx + sw, y1 = dy + sh;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x0 >= x1 || y0 >= y1)
        return;

    const int n = x1 - x0;
    for (int y = y0; y < y1; y++) {
        int ly = y - dy;
        int srow = sy + (flip_y ? sh - 1 - ly : ly);
        uint16_t *dst = g->px + (uint32_t)y * g->stride + x0;
        const uint32_t sbase = (uint32_t)srow * s->w;
        if (!flip_x) {
            const uint16_t *src = s->px + sbase + sx + (x0 - dx);
            if (opaque) {
                memcpy(dst, src, (size_t)n * 2);
            } else {
                const uint8_t *a = s->alpha + sbase + sx + (x0 - dx);
                for (int i = 0; i < n; i++)
                    if (a[i]) dst[i] = src[i];
            }
        } else {
            int scol = sx + sw - 1 - (x0 - dx);
            const uint16_t *src = s->px + sbase;
            const uint8_t *a = s->alpha + sbase;
            for (int i = 0; i < n; i++, scol--)
                if (opaque || a[scol]) dst[i] = src[scol];
        }
    }
}

static int clamp_rect(const g16_sheet_t *s, int *sx, int *sy, int *sw, int *sh)
{
    if (*sx < 0 || *sy < 0 || *sw <= 0 || *sh <= 0)
        return 0;
    if (*sx + *sw > s->w) *sw = s->w - *sx;
    if (*sy + *sh > s->h) *sh = s->h - *sy;
    return *sw > 0 && *sh > 0;
}

void g16_spr(g16_t *g, const g16_sheet_t *s, int n, int x, int y,
             int wc, int hc, int flip_x, int flip_y)
{
    if (!s->px || n < 0) return;
    int per_row = s->w / G16_CELL;
    int sx = n % per_row * G16_CELL, sy = n / per_row * G16_CELL;
    int sw = wc * G16_CELL, sh = hc * G16_CELL;
    if (!clamp_rect(s, &sx, &sy, &sw, &sh)) return;
    int opaque = wc == 1 && hc == 1 && s->cell_opaque[n];
    blit(g, s, sx, sy, sw, sh, x - g->cam_x, y - g->cam_y, flip_x, flip_y, opaque);
}

void g16_sspr(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
              int dx, int dy, int flip_x, int flip_y)
{
    if (!s->px || !clamp_rect(s, &sx, &sy, &sw, &sh)) return;
    blit(g, s, sx, sy, sw, sh, dx - g->cam_x, dy - g->cam_y, flip_x, flip_y, 0);
}

void g16_sspr_zoom(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                   int dx, int dy, int flip_x, int flip_y, float zoom)
{
    if (!s->px || !(zoom > 0.0f) || zoom > 4096.0f || !clamp_rect(s, &sx, &sy, &sw, &sh)) return;
    int dw = (int)(sw * zoom + 0.5f), dh = (int)(sh * zoom + 0.5f);
    dx -= g->cam_x;
    dy -= g->cam_y;
    if (dw == sw && dh == sh) {
        blit(g, s, sx, sy, sw, sh, dx, dy, flip_x, flip_y, 0);
        return;
    }
    if (dw <= 0 || dh <= 0) return;
    int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x0 >= x1 || y0 >= y1) return;
    /* source pixel of a screen pixel: (offset * step) >> 16, with the step
     * rounded up so that whole zooms land exactly on their pixels */
    const uint32_t stepx = (uint32_t)((((uint64_t)sw << 16) + (uint32_t)dw - 1) / (uint32_t)dw);
    const uint32_t stepy = (uint32_t)((((uint64_t)sh << 16) + (uint32_t)dh - 1) / (uint32_t)dh);
    for (int y = y0; y < y1; y++) {
        int ly = (int)(((uint64_t)(uint32_t)(y - dy) * stepy) >> 16);
        if (ly >= sh) ly = sh - 1;
        const uint32_t sbase = (uint32_t)(sy + (flip_y ? sh - 1 - ly : ly)) * s->w;
        uint16_t *dst = g->px + (uint32_t)y * g->stride;
        uint64_t fx = (uint64_t)(uint32_t)(x0 - dx) * stepx;
        for (int x = x0; x < x1; x++, fx += stepx) {
            int lx = (int)(fx >> 16);
            if (lx >= sw) lx = sw - 1;
            uint32_t k = sbase + (uint32_t)(sx + (flip_x ? sw - 1 - lx : lx));
            if (s->alpha[k])
                dst[x] = s->px[k];
        }
    }
}

void g16_map(g16_t *g, const g16_sheet_t *s, const g16_map_t *m,
             int mx, int my, int x, int y, int mw, int mh)
{
    if (!s->px || !m->cells) return;
    const int per_row = s->w / G16_CELL, ncells = per_row * (s->h / G16_CELL);
    x -= g->cam_x;
    y -= g->cam_y;
    for (int cy = 0; cy < mh; cy++) {
        int row = my + cy, py = y + cy * G16_CELL;
        if (row < 0 || row >= m->h || py >= g->cy1 || py + G16_CELL <= g->cy0) continue;
        for (int cx = 0; cx < mw; cx++) {
            int col = mx + cx, px = x + cx * G16_CELL;
            if (col < 0 || col >= m->w || px >= g->cx1 || px + G16_CELL <= g->cx0) continue;
            int n = m->cells[row * m->w + col];
            if (n == 0 || n >= ncells) continue;
            blit(g, s, n % per_row * G16_CELL, n / per_row * G16_CELL, G16_CELL, G16_CELL,
                 px, py, 0, 0, s->cell_opaque[n]);
        }
    }
}

int g16_text(g16_t *g, int x, int y, const char *str, uint16_t c)
{
    const font_t *f = g->font;
    int sx = x - g->cam_x, sy = y - g->cam_y;
    const int cw = f->width;
    for (; *str; str++, sx += cw, x += cw) {
        if (*str == '\n') { sy += f->height; sx = x = x - cw; continue; }
        if (sx >= g->cx1 || sx + cw <= g->cx0 || sy >= g->cy1 || sy + f->height <= g->cy0)
            continue;
        const uint8_t *gl = f->glyphs + (uint8_t)*str * f->height;
        for (int r = 0; r < f->height; r++) {
            uint8_t bits = gl[r];
            for (int b = 0; bits; b++, bits <<= 1)
                if (bits & 0x80) plot(g, sx + b, sy + r, c);
        }
    }
    return x;
}

int g16_text_scaled(g16_t *g, int x, int y, const char *str, uint16_t c, int scale)
{
    if (scale <= 1)
        return g16_text(g, x, y, str, c);
    const font_t *f = g->font;
    const int cw = f->width * scale, ch = f->height * scale;
    int x0 = x;
    for (; *str; str++, x += cw) {
        if (*str == '\n') { y += ch; x = x0 - cw; continue; }
        int sx = x - g->cam_x, sy = y - g->cam_y;
        if (sx >= g->cx1 || sx + cw <= g->cx0 || sy >= g->cy1 || sy + ch <= g->cy0)
            continue;
        const uint8_t *gl = f->glyphs + (uint8_t)*str * f->height;
        for (int r = 0; r < f->height; r++) {
            uint8_t bits = gl[r];
            /* runs of lit pixels: one rectangle each */
            for (int b = 0; b < 8;) {
                if (!(bits & (0x80 >> b))) { b++; continue; }
                int e = b;
                while (e < 8 && (bits & (0x80 >> e))) e++;
                g16_rectfill(g, x + b * scale, y + r * scale, (e - b) * scale, scale, c);
                b = e;
            }
        }
    }
    return x;
}

static uint32_t sheet_versions;

int g16_sheet_alloc(g16_sheet_t *s, int w, int h)
{
    memset(s, 0, sizeof *s);
    s->version = sheet_versions += 0x10000;
    w = (w + G16_CELL - 1) / G16_CELL * G16_CELL;
    h = (h + G16_CELL - 1) / G16_CELL * G16_CELL;
    s->px = calloc((size_t)w * h, 2);
    s->alpha = calloc((size_t)w * h, 1);
    s->cell_opaque = calloc((size_t)(w / G16_CELL) * (h / G16_CELL), 1);
    if (!s->px || !s->alpha || !s->cell_opaque) {
        g16_sheet_free(s);
        return -1;
    }
    s->w = w;
    s->h = h;
    return 0;
}

void g16_sheet_free(g16_sheet_t *s)
{
    free(s->px);
    free(s->alpha);
    free(s->cell_opaque);
    memset(s, 0, sizeof *s);
}

void g16_sheet_set(g16_sheet_t *s, int x, int y, uint16_t c, int opaque)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    uint32_t i = (uint32_t)y * s->w + x;
    s->px[i] = c;
    s->alpha[i] = opaque ? 1 : 0;
    s->version++;
    if (!opaque)
        s->cell_opaque[(y / G16_CELL) * (s->w / G16_CELL) + x / G16_CELL] = 0;
}

void g16_sheet_update_cell(g16_sheet_t *s, int cx, int cy)
{
    int per_row = s->w / G16_CELL;
    if (cx < 0 || cy < 0 || cx >= per_row || cy >= s->h / G16_CELL) return;
    int all = 1;
    for (int y = 0; y < G16_CELL && all; y++)
        for (int x = 0; x < G16_CELL; x++)
            if (!s->alpha[(uint32_t)(cy * G16_CELL + y) * s->w + cx * G16_CELL + x]) { all = 0; break; }
    s->cell_opaque[cy * per_row + cx] = (uint8_t)all;
    s->version++;
}

/* ---------------------------------------------------------------- light */

#define LCELL 4
#define LMAX  512                   /* 2x */

int g16_light_init(g16_light_t *l, int w, int h)
{
    l->w = w;
    l->h = h;
    l->nx = w / LCELL + 1;
    l->ny = h / LCELL + 1;
    l->rgb = malloc((size_t)l->nx * l->ny * 3 * sizeof *l->rgb);
    return l->rgb ? 0 : -1;
}

void g16_light_free(g16_light_t *l)
{
    free(l->rgb);
    l->rgb = NULL;
}

static uint16_t chan(uint32_t v)            /* 0..255 -> 0..256 */
{
    return (uint16_t)(v + (v >> 7));
}

void g16_light_clear(g16_light_t *l, uint32_t amb)
{
    uint16_t r = chan(amb >> 16 & 255), g = chan(amb >> 8 & 255), b = chan(amb & 255);
    uint16_t *p = l->rgb;
    for (int i = l->nx * l->ny; i > 0; i--, p += 3) {
        p[0] = r; p[1] = g; p[2] = b;
    }
}

void g16_light_add(g16_light_t *l, float x, float y, float radius, uint32_t rgb, float k)
{
    if (radius <= 0 || k <= 0)
        return;
    int x0 = (int)((x - radius) / LCELL), x1 = (int)((x + radius) / LCELL) + 1;
    int y0 = (int)((y - radius) / LCELL), y1 = (int)((y + radius) / LCELL) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= l->nx) x1 = l->nx - 1;
    if (y1 >= l->ny) y1 = l->ny - 1;
    const float inv = 1.0f / (radius * radius);
    const float cr = (rgb >> 16 & 255) * k, cg = (rgb >> 8 & 255) * k, cb = (rgb & 255) * k;
    for (int j = y0; j <= y1; j++) {
        float dy = j * LCELL - y;
        uint16_t *p = l->rgb + ((size_t)j * l->nx + x0) * 3;
        for (int i = x0; i <= x1; i++, p += 3) {
            float dx = i * LCELL - x;
            float t = 1.0f - (dx * dx + dy * dy) * inv;
            if (t <= 0)
                continue;
            t *= t;                             /* soft edge */
            int r = p[0] + (int)(cr * t), g = p[1] + (int)(cg * t), b = p[2] + (int)(cb * t);
            p[0] = (uint16_t)(r > LMAX ? LMAX : r);
            p[1] = (uint16_t)(g > LMAX ? LMAX : g);
            p[2] = (uint16_t)(b > LMAX ? LMAX : b);
        }
    }
}

/* 4x4 ordered dither, 0..15 scaled to the 8 fractional bits */
static const uint8_t bayer[4][4] = {
    { 0, 128, 32, 160 }, { 192, 64, 224, 96 }, { 48, 176, 16, 144 }, { 240, 112, 208, 80 },
};

void g16_light_apply(g16_t *g, const g16_light_t *l)
{
    const int w = g->w < l->w ? g->w : l->w, h = g->h < l->h ? g->h : l->h;
    for (int by = 0; by < h; by += LCELL) {
        const uint16_t *top = l->rgb + (size_t)(by / LCELL) * l->nx * 3;
        const uint16_t *bot = top + l->nx * 3;
        for (int bx = 0; bx < w; bx += LCELL) {
            const uint16_t *a = top + (bx / LCELL) * 3, *b = a + 3, *c = bot + (bx / LCELL) * 3, *d = c + 3;
            for (int j = 0; j < LCELL && by + j < h; j++) {
                uint16_t *row = g->px + (uint32_t)(by + j) * g->stride + bx;
                int lr = a[0] * (LCELL - j) + c[0] * j, rr = b[0] * (LCELL - j) + d[0] * j;
                int lg = a[1] * (LCELL - j) + c[1] * j, rg = b[1] * (LCELL - j) + d[1] * j;
                int lb = a[2] * (LCELL - j) + c[2] * j, rb = b[2] * (LCELL - j) + d[2] * j;
                for (int i = 0; i < LCELL && bx + i < w; i++) {
                    /* light x 16 (bilinear weights sum to 16) */
                    int vr = lr * (LCELL - i) + rr * i, vg = lg * (LCELL - i) + rg * i;
                    int vb = lb * (LCELL - i) + rb * i;
                    uint32_t p = row[i], dth = bayer[j][i];
                    uint32_t r5 = ((p >> 11) * (uint32_t)vr / 16 + dth) >> 8;
                    uint32_t g6 = ((p >> 5 & 63) * (uint32_t)vg / 16 + dth) >> 8;
                    uint32_t b5 = ((p & 31) * (uint32_t)vb / 16 + dth) >> 8;
                    if (r5 > 31) r5 = 31;
                    if (g6 > 63) g6 = 63;
                    if (b5 > 31) b5 = 31;
                    row[i] = (uint16_t)(r5 << 11 | g6 << 5 | b5);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- fades */

int g16_fade_init(g16_fade_t *f, int w, int h)
{
    memset(f, 0, sizeof *f);
    f->w = w;
    f->h = h;
    f->lv = malloc((size_t)w * h);
    f->index = malloc(65536);
    f->tab = malloc(G16_FADE_LEVELS * 256 * sizeof *f->tab);
    if (!f->lv || !f->index || !f->tab) {
        g16_fade_free(f);
        return -1;
    }
    g16_fade_reset(f, 8);
    g16_fade_done(f);
    return 0;
}

void g16_fade_free(g16_fade_t *f)
{
    free(f->lv);
    free(f->index);
    free(f->tab);
    f->lv = f->index = NULL;
    f->tab = NULL;
}

void g16_fade_reset(g16_fade_t *f, int levels)
{
    f->levels = levels < 2 ? 2 : levels > G16_FADE_LEVELS ? G16_FADE_LEVELS : levels;
    f->ncol = 0;
    memset(f->index, 255, 65536);
}

int g16_fade_colour(g16_fade_t *f, uint16_t from, const uint16_t *to)
{
    int i = f->index[from];
    if (i == 255) {
        if (f->ncol >= G16_FADE_COLOURS)
            return -1;
        i = f->ncol++;
        f->index[from] = (uint8_t)i;
        f->from[i] = from;
    }
    for (int k = 0; k < f->levels; k++)
        f->tab[k * 256 + i] = to[k];
    return 0;
}

void g16_fade_done(g16_fade_t *f)
{
    /* the average ratio, channel by channel, of the colours with a table */
    for (int k = 0; k < f->levels; k++) {
        uint32_t num[3] = { 0, 0, 0 }, den[3] = { 0, 0, 0 };
        for (int i = 0; i < f->ncol; i++) {
            const uint32_t c = f->from[i];
            const uint16_t t = f->tab[k * 256 + i];
            num[0] += t >> 11;      den[0] += (uint32_t)c >> 11;
            num[1] += t >> 5 & 63;  den[1] += (uint32_t)c >> 5 & 63;
            num[2] += t & 31;       den[2] += (uint32_t)c & 31;
        }
        for (int ch = 0; ch < 3; ch++) {
            uint32_t m = den[ch] ? num[ch] * 256 / den[ch] : (uint32_t)(256 * (k + 1) / f->levels);
            f->mul[k][ch] = (uint16_t)(m > 512 ? 512 : m);
        }
    }
}

void g16_fade_clear(g16_fade_t *f, int ambient)
{
    if (ambient < 0) ambient = 0;
    if (ambient >= f->levels) ambient = f->levels - 1;
    memset(f->lv, ambient, (size_t)f->w * f->h);
}

/* 1 - sqrt(i / 1024), x 256 */
static uint16_t fade_root[1025];

static int isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (int)r;
}

void g16_fade_glow(g16_fade_t *f, int x, int y, int radius, int level, int dither)
{
    if (radius <= 0 || level <= 0)
        return;
    if (!fade_root[0]) {
        for (int i = 0; i <= 1024; i++)
            fade_root[i] = (uint16_t)(256 - isqrt32((uint32_t)i * 65536u) / 32);  /* sqrt(i/1024) x 256 */
    }
    if (level >= f->levels) level = f->levels - 1;
    if (radius > 1024) radius = 1024;
    if (dither < 0) dither = 0;
    if (dither > 256) dither = 256;
    const uint32_t r2 = (uint32_t)radius * radius;
    const uint32_t inv = (1024u << 16) / r2;       /* d2 * inv >> 16: 0..1024 */
    int y0 = y - radius + 1, y1 = y + radius - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= f->h) y1 = f->h - 1;
    /* the dither offset of each 4x4 position, in 1/256 of a level */
    int off[4][4];
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            off[j][i] = 128 + (((int)bayer[j][i] + 8 - 128) * dither) / 256;
    for (int py = y0; py <= y1; py++) {
        const int dy = py - y;
        const uint32_t dy2 = (uint32_t)(dy * dy);
        if (dy2 >= r2)
            continue;
        const int hw = isqrt32(r2 - dy2);
        int x0 = x - hw, x1 = x + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= f->w) x1 = f->w - 1;
        uint8_t *row = f->lv + (size_t)py * f->w;
        const int *o = off[py & 3];
        for (int px = x0; px <= x1; px++) {
            const int dx = px - x;
            uint32_t t = ((uint32_t)(dx * dx) + dy2) * inv >> 16;
            if (t > 1024) t = 1024;
            int lv = (level * fade_root[t] + o[px & 3]) >> 8;
            if (lv > row[px])
                row[px] = (uint8_t)lv;
        }
    }
}

void g16_fade_apply(g16_t *g, const g16_fade_t *f)
{
    const int w = g->w < f->w ? g->w : f->w, h = g->h < f->h ? g->h : f->h;
    for (int y = 0; y < h; y++) {
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        const uint8_t *lv = f->lv + (size_t)y * f->w;
        for (int x = 0; x < w; x++) {
            const uint16_t c = row[x];
            const int k = lv[x], i = f->index[c];
            if (i != 255) {
                row[x] = f->tab[k * 256 + i];
            } else {
                const uint16_t *m = f->mul[k];
                uint32_t r5 = ((uint32_t)(c >> 11) * m[0]) >> 8;
                uint32_t g6 = ((uint32_t)(c >> 5 & 63) * m[1]) >> 8;
                uint32_t b5 = ((uint32_t)(c & 31) * m[2]) >> 8;
                if (r5 > 31) r5 = 31;
                if (g6 > 63) g6 = 63;
                if (b5 > 31) b5 = 31;
                row[x] = (uint16_t)(r5 << 11 | g6 << 5 | b5);
            }
        }
    }
}
