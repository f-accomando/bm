#include "gfx16.h"

#include <math.h>
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
    g->blend_mode = G16_BLEND_NORMAL;
    g->global_alpha = 255;
    g->palt_mask = 0x0001;
    g->color_key = -1;
    g16_clip(g, 0, 0, 0, 0);
}

void g16_blend(g16_t *g, g16_blend_mode_t mode, uint8_t global_alpha)
{
    if (g) {
        g->blend_mode = mode;
        g->global_alpha = global_alpha;
    }
}

void g16_palt(g16_t *g, int c, int transparent)
{
    if (!g) return;
    if (c >= 0 && c < 16) {
        if (transparent)
            g->palt_mask |= (uint16_t)(1u << c);
        else
            g->palt_mask &= (uint16_t)~(1u << c);
    } else if (c >= 0) {
        if (transparent)
            g->color_key = c & 0xFFFF;
        else if (g->color_key == (c & 0xFFFF))
            g->color_key = -1;
    }
}

void g16_palt_reset(g16_t *g)
{
    if (!g) return;
    g->palt_mask = 0x0001;
    g->color_key = -1;
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
    uint16_t *p = g->px + (uint32_t)y * g->stride + x0;
    int n = x1 - x0 + 1;
    if (g->blend_mode == G16_BLEND_NORMAL && g->global_alpha == 255) {
        span(p, n, c);
    } else {
        g16_blend_mode_t bm = g->blend_mode;
        uint8_t a = g->global_alpha;
        for (int i = 0; i < n; i++)
            p[i] = g16_blend_pixel(c, p[i], bm, a);
    }
}

static inline void vline(g16_t *g, int x, int y0, int y1, uint16_t c)
{
    if (x < g->cx0 || x >= g->cx1) return;
    if (y0 < g->cy0) y0 = g->cy0;
    if (y1 >= g->cy1) y1 = g->cy1 - 1;
    if (g->blend_mode == G16_BLEND_NORMAL && g->global_alpha == 255) {
        for (int y = y0; y <= y1; y++)
            g->px[(uint32_t)y * g->stride + x] = c;
    } else {
        g16_blend_mode_t bm = g->blend_mode;
        uint8_t a = g->global_alpha;
        for (int y = y0; y <= y1; y++) {
            uint16_t *p = &g->px[(uint32_t)y * g->stride + x];
            *p = g16_blend_pixel(c, *p, bm, a);
        }
    }
}

static inline void plot(g16_t *g, int x, int y, uint16_t c)             /* screen coords */
{
    if (x >= g->cx0 && x < g->cx1 && y >= g->cy0 && y < g->cy1) {
        uint16_t *p = &g->px[(uint32_t)y * g->stride + x];
        if (g->blend_mode == G16_BLEND_NORMAL && g->global_alpha == 255)
            *p = c;
        else
            *p = g16_blend_pixel(c, *p, g->blend_mode, g->global_alpha);
    }
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

void g16_blit565(g16_t *g, const uint16_t *src, int w, int h, int x, int y, int scale)
{
    if (w <= 0 || h <= 0 || scale < 1 || scale > 8)
        return;
    const int64_t lx = (int64_t)x - g->cam_x, ly = (int64_t)y - g->cam_y;
    const int dw = w * scale, dh = h * scale;
    if (lx >= g->cx1 || ly >= g->cy1 || lx + dw <= g->cx0 || ly + dh <= g->cy0)
        return;                         /* off the clip (and no overflow below) */
    x = (int)lx;
    y = (int)ly;
    int x0 = x < g->cx0 ? g->cx0 : x, x1 = x + dw > g->cx1 ? g->cx1 : x + dw;
    int y0 = y < g->cy0 ? g->cy0 : y, y1 = y + dh > g->cy1 ? g->cy1 : y + dh;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (scale == 1) {
        for (int yy = y0; yy < y1; yy++) {
            const uint16_t *row = src + (size_t)(yy - y) * w;
            uint16_t *out = g->px + (size_t)yy * g->stride;
            memcpy(out + x0, row + (x0 - x), (size_t)(x1 - x0) * 2);
        }
        return;
    }
    const int span_w = x1 - x0;
    int16_t xmap_buf[1920];
    int16_t *xmap = span_w <= 1920 ? xmap_buf : malloc((size_t)span_w * sizeof(int16_t));
    if (!xmap) return;
    for (int i = 0; i < span_w; i++)
        xmap[i] = (int16_t)((x0 + i - x) / scale);
    for (int yy = y0; yy < y1; yy++) {
        const uint16_t *row = src + (size_t)((yy - y) / scale) * w;
        uint16_t *out = g->px + (size_t)yy * g->stride + x0;
        for (int i = 0; i < span_w; i++)
            out[i] = row[xmap[i]];
    }
    if (xmap != xmap_buf)
        free(xmap);
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

void g16_rrect(g16_t *g, int x, int y, int w, int h, int r, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { g16_rect(g, x, y, w, h, c); return; }
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    int sx = x - g->cam_x, sy = y - g->cam_y;
    int x0 = sx, x1 = sx + w - 1;
    int y0 = sy, y1 = sy + h - 1;
    hline(g, x0 + r, x1 - r, y0, c);
    hline(g, x0 + r, x1 - r, y1, c);
    vline(g, x0, y0 + r, y1 - r, c);
    vline(g, x1, y0 + r, y1 - r, c);
    int cx = r, cy = 0, err = 1 - r;
    while (cx >= cy) {
        plot(g, x0 + r - cx, y0 + r - cy, c);
        plot(g, x0 + r - cy, y0 + r - cx, c);
        plot(g, x1 - r + cx, y0 + r - cy, c);
        plot(g, x1 - r + cy, y0 + r - cx, c);
        plot(g, x0 + r - cx, y1 - r + cy, c);
        plot(g, x0 + r - cy, y1 - r + cx, c);
        plot(g, x1 - r + cx, y1 - r + cy, c);
        plot(g, x1 - r + cy, y1 - r + cx, c);
        cy++;
        if (err < 0) err += 2 * cy + 1;
        else { cx--; err += 2 * (cy - cx) + 1; }
    }
}

void g16_rrectfill(g16_t *g, int x, int y, int w, int h, int r, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { g16_rectfill(g, x, y, w, h, c); return; }
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    int sx = x - g->cam_x, sy = y - g->cam_y;
    int x0 = sx, x1 = sx + w - 1;
    int y0 = sy, y1 = sy + h - 1;
    int max_x[1024];
    int *mx = r < 1024 ? max_x : malloc((size_t)(r + 1) * sizeof(int));
    if (!mx) return;
    for (int i = 0; i <= r; i++) mx[i] = 0;
    int cx = r, cy = 0, err = 1 - r;
    while (cx >= cy) {
        if (cx > mx[cy]) mx[cy] = cx;
        if (cy > mx[cx]) mx[cy] = cy;
        cy++;
        if (err < 0) err += 2 * cy + 1;
        else { cx--; err += 2 * (cy - cx) + 1; }
    }
    for (int dy = 1; dy <= r; dy++) {
        int dx = mx[dy];
        hline(g, x0 + r - dx, x1 - r + dx, y0 + r - dy, c);
        hline(g, x0 + r - dx, x1 - r + dx, y1 - r + dy, c);
    }
    for (int yy = y0 + r; yy <= y1 - r; yy++)
        hline(g, x0, x1, yy, c);
    if (mx != max_x) free(mx);
}

void g16_ellipse(g16_t *g, int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx <= 0 || ry <= 0) return;
    cx -= g->cam_x;
    cy -= g->cam_y;
    int64_t rx2 = (int64_t)rx * rx, ry2 = (int64_t)ry * ry;
    int64_t rx2_2 = 2 * rx2, ry2_2 = 2 * ry2;
    int x = 0, y = ry;
    int64_t px = 0, py = rx2_2 * y;
    int64_t p = ry2 - rx2 * ry + (rx2 >> 2);
    while (px < py) {
        plot(g, cx + x, cy + y, c);
        plot(g, cx - x, cy + y, c);
        plot(g, cx + x, cy - y, c);
        plot(g, cx - x, cy - y, c);
        x++;
        px += ry2_2;
        if (p < 0)
            p += ry2 + px;
        else {
            y--;
            py -= rx2_2;
            p += ry2 + px - py;
        }
    }
    p = (int64_t)(ry2 * (x * 2 + 1) * (x * 2 + 1) + 4 * rx2 * (y - 1) * (y - 1) - 4 * rx2 * ry2 + 2) / 4;
    while (y >= 0) {
        plot(g, cx + x, cy + y, c);
        plot(g, cx - x, cy + y, c);
        plot(g, cx + x, cy - y, c);
        plot(g, cx - x, cy - y, c);
        y--;
        py -= rx2_2;
        if (p > 0)
            p += rx2 - py;
        else {
            x++;
            px += ry2_2;
            p += rx2 - py + px;
        }
    }
}

void g16_ellipsefill(g16_t *g, int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx <= 0 || ry <= 0) return;
    cx -= g->cam_x;
    cy -= g->cam_y;
    int max_x[1024];
    int *mx = ry < 1024 ? max_x : malloc((size_t)(ry + 1) * sizeof(int));
    if (!mx) return;
    for (int i = 0; i <= ry; i++) mx[i] = 0;
    int64_t rx2 = (int64_t)rx * rx, ry2 = (int64_t)ry * ry;
    int64_t rx2_2 = 2 * rx2, ry2_2 = 2 * ry2;
    int x = 0, y = ry;
    int64_t px = 0, py = rx2_2 * y;
    int64_t p = ry2 - rx2 * ry + (rx2 >> 2);
    while (px < py) {
        if (x > mx[y]) mx[y] = x;
        x++;
        px += ry2_2;
        if (p < 0)
            p += ry2 + px;
        else {
            y--;
            py -= rx2_2;
            p += ry2 + px - py;
        }
    }
    p = (int64_t)(ry2 * (x * 2 + 1) * (x * 2 + 1) + 4 * rx2 * (y - 1) * (y - 1) - 4 * rx2 * ry2 + 2) / 4;
    while (y >= 0) {
        if (x > mx[y]) mx[y] = x;
        y--;
        py -= rx2_2;
        if (p > 0)
            p += rx2 - py;
        else {
            x++;
            px += ry2_2;
            p += rx2 - py + px;
        }
    }
    hline(g, cx - mx[0], cx + mx[0], cy, c);
    for (int dy = 1; dy <= ry; dy++) {
        hline(g, cx - mx[dy], cx + mx[dy], cy + dy, c);
        hline(g, cx - mx[dy], cx + mx[dy], cy - dy, c);
    }
    if (mx != max_x) free(mx);
}

typedef uint32_t __attribute__((may_alias)) g16_w32;

static inline void draw_pixel_with_alpha(g16_t *g, uint16_t *dst, uint16_t c, uint8_t a)
{
    if (!a) return;
    if ((g->palt_mask != 0x0001) && (c < 16) && ((g->palt_mask >> c) & 1)) return;
    if (g->color_key >= 0 && (int32_t)c == g->color_key) return;
    uint8_t eff_a = (a == 1 || a == 255) ? g->global_alpha : (uint8_t)((a * (int)g->global_alpha + 128) >> 8);
    if (!eff_a) return;
    if (g->blend_mode == G16_BLEND_NORMAL && eff_a == 255)
        *dst = c;
    else
        *dst = g16_blend_pixel(c, *dst, g->blend_mode, eff_a);
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
    const int fast_normal = (g->blend_mode == G16_BLEND_NORMAL && g->global_alpha == 255 && g->palt_mask == 0x0001 && g->color_key < 0);

    for (int y = y0; y < y1; y++) {
        int ly = y - dy;
        int srow = sy + (flip_y ? sh - 1 - ly : ly);
        uint16_t *dst = g->px + (uint32_t)y * g->stride + x0;
        const uint32_t sbase = (uint32_t)srow * s->w;
        if (!flip_x) {
            const uint16_t *src = s->px + sbase + sx + (x0 - dx);
            if (fast_normal) {
                if (opaque) {
                    memcpy(dst, src, (size_t)n * 2);
                } else {
                    const uint8_t *a = s->alpha + sbase + sx + (x0 - dx);
                    for (int i = 0; i < n; i++)
                        if (a[i]) dst[i] = src[i];
                }
            } else {
                const uint8_t *a = s->alpha ? (s->alpha + sbase + sx + (x0 - dx)) : NULL;
                for (int i = 0; i < n; i++)
                    draw_pixel_with_alpha(g, &dst[i], src[i], a ? a[i] : 255);
            }
        } else {
            int scol = sx + sw - 1 - (x0 - dx);
            const uint16_t *src = s->px + sbase;
            const uint8_t *a = s->alpha ? (s->alpha + sbase) : NULL;
            if (fast_normal) {
                for (int i = 0; i < n; i++, scol--)
                    if (opaque || a[scol]) dst[i] = src[scol];
            } else {
                for (int i = 0; i < n; i++, scol--)
                    draw_pixel_with_alpha(g, &dst[i], src[scol], a ? a[scol] : 255);
            }
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
    const int span_w = x1 - x0;
    int stack_xmap[1024];
    int *xmap = span_w <= 1024 ? stack_xmap : malloc((size_t)span_w * sizeof(int));
    if (!xmap) return;
    uint64_t fx = (uint64_t)(uint32_t)(x0 - dx) * stepx;
    for (int i = 0; i < span_w; i++, fx += stepx) {
        int lx = (int)(fx >> 16);
        if (lx >= sw) lx = sw - 1;
        xmap[i] = (int)(flip_x ? sw - 1 - lx : lx);
    }
    for (int y = y0; y < y1; y++) {
        int ly = (int)(((uint64_t)(uint32_t)(y - dy) * stepy) >> 16);
        if (ly >= sh) ly = sh - 1;
        const uint32_t sbase = (uint32_t)(sy + (flip_y ? sh - 1 - ly : ly)) * s->w + (uint32_t)sx;
        uint16_t *dst = g->px + (uint32_t)y * g->stride;
        for (int i = 0; i < span_w; i++) {
            uint32_t k = sbase + (uint32_t)xmap[i];
            draw_pixel_with_alpha(g, &dst[x0 + i], s->px[k], s->alpha[k]);
        }
    }
    if (xmap != stack_xmap) free(xmap);
}

/* Sprite stacking: slice i of `n` (left to right on the sheet, or top to bottom for
 * g16_stackv; slice 0 is the
 * bottom) is drawn turned by `rz` radians around its centre, `scale` times
 * bigger, and lifted by i * step * scale pixels.  Inverse mapping: for each
 * screen pixel of the bounding box, the source pixel comes from 16.16 fixed
 * point steps (sin and cos are read once per call). */
static inline void stack_core(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                              int n, int x, int y, float rz, float scale, float step, int vert)
{
    if (!s->px || n <= 0 || sw <= 0 || sh <= 0 || sx < 0 || sy < 0) return;
    if (vert ? sx + sw > s->w : sy + sh > s->h) return;
    if (!isfinite(rz) || !isfinite(scale) || !isfinite(step)) return;   /* NaN or inf from Lua */
    if (!(scale > 0.0f) || scale > 64.0f) return;
    const float c = cosf(rz), sn = sinf(rz);
    const float ac = c < 0 ? -c : c, as = sn < 0 ? -sn : sn;
    /* a turn of exactly 90 degrees must not leave a rounding trace */
    const int32_t cf = (int32_t)lrintf(c / scale * 65536.0f);
    const int32_t sf = (int32_t)lrintf(sn / scale * 65536.0f);
    /* half size of the bounding box of the turned rectangle */
    const float hx = scale * (ac * sw + as * sh) * 0.5f + 1.0f;
    const float hy = scale * (as * sw + ac * sh) * 0.5f + 1.0f;
    /* the centre of the slice, in the index space of the pixels */
    const float cx0 = (float)(x - g->cam_x - sw / 2) + (float)(sw - 1) * 0.5f;
    const float cy0 = (float)(y - g->cam_y - sh / 2) + (float)(sh - 1) * 0.5f;
    const float hw = (float)(sw - 1) * 0.5f, hh = (float)(sh - 1) * 0.5f;
    for (int i = 0; i < n; i++) {
        /* the slice's box on the sheet: to the right (stack) or below (stackv); a per-slice
         * cost only, the pixel loop is the same */
        const int fx = vert ? sx : sx + i * sw;
        const int fy = vert ? sy + i * sh : sy;
        if (fx + sw > s->w || fy + sh > s->h) break;
        /* a lift beyond 1e6 pixels is off screen anyway: the clamp keeps the cast defined */
        float lift = floorf((float)i * step * scale + 0.5f);
        if (!(lift > -1.0e6f)) lift = -1.0e6f;
        if (lift > 1.0e6f) lift = 1.0e6f;
        const float cy = cy0 - (float)(int)lift;
        const float cx = cx0;
        int x0 = (int)floorf(cx - hx), x1 = (int)ceilf(cx + hx) + 1;
        int y0 = (int)floorf(cy - hy), y1 = (int)ceilf(cy + hy) + 1;
        if (x0 < g->cx0) x0 = g->cx0;
        if (y0 < g->cy0) y0 = g->cy0;
        if (x1 > g->cx1) x1 = g->cx1;
        if (y1 > g->cy1) y1 = g->cy1;
        if (x0 >= x1 || y0 >= y1) continue;
        /* source position (+0.5 so that the cut rounds to the nearest pixel) */
        const float ox = (float)x0 - cx, oy = (float)y0 - cy;
        int32_t u0 = (int32_t)lrintf((ox * (float)cf + oy * (float)sf) + (hw + 0.5f) * 65536.0f);
        int32_t v0 = (int32_t)lrintf((oy * (float)cf - ox * (float)sf) + (hh + 0.5f) * 65536.0f);
        for (int py = y0; py < y1; py++, u0 += sf, v0 += cf) {
            int32_t u = u0, v = v0;
            uint16_t *dst = g->px + (uint32_t)py * g->stride;
            for (int px = x0; px < x1; px++, u += cf, v -= sf) {
                const uint32_t lx = (uint32_t)(u >> 16), ly = (uint32_t)(v >> 16);
                if (u < 0 || v < 0 || lx >= (uint32_t)sw || ly >= (uint32_t)sh) continue;
                const uint32_t k = (uint32_t)(fy + (int)ly) * s->w + (uint32_t)(fx + (int)lx);
                draw_pixel_with_alpha(g, &dst[px], s->px[k], s->alpha[k]);
            }
        }
    }
}

void g16_stack(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
               int n, int x, int y, float rz, float scale, float step)
{
    stack_core(g, s, sx, sy, sw, sh, n, x, y, rz, scale, step, 0);
}

void g16_stackv(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                int n, int x, int y, float rz, float scale, float step)
{
    stack_core(g, s, sx, sy, sw, sh, n, x, y, rz, scale, step, 1);
}

void g16_sspr_affine(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                     int dx, int dy, float a, float b, float c, float d, float ox, float oy)
{
    if (!s->px || sw <= 0 || sh <= 0 || sx < 0 || sy < 0) return;
    if (sx + sw > s->w || sy + sh > s->h) return;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d)) return;
    if (!isfinite(ox) || !isfinite(oy)) return;
    float det = a * d - b * c;
    if (det == 0.0f || !isfinite(det)) return;

    const float cx = (float)(dx - g->cam_x);
    const float cy = (float)(dy - g->cam_y);

    const float u0 = -ox, v0_c = -oy;
    const float u1 = (float)sw - ox, v1_c = (float)sh - oy;

    const float x0_r = a * u0 + b * v0_c, y0_r = c * u0 + d * v0_c;
    const float x1_r = a * u1 + b * v0_c, y1_r = c * u1 + d * v0_c;
    const float x2_r = a * u1 + b * v1_c, y2_r = c * u1 + d * v1_c;
    const float x3_r = a * u0 + b * v1_c, y3_r = c * u0 + d * v1_c;

    float min_x = x0_r, max_x = x0_r;
    if (x1_r < min_x) min_x = x1_r;
    if (x1_r > max_x) max_x = x1_r;
    if (x2_r < min_x) min_x = x2_r;
    if (x2_r > max_x) max_x = x2_r;
    if (x3_r < min_x) min_x = x3_r;
    if (x3_r > max_x) max_x = x3_r;

    float min_y = y0_r, max_y = y0_r;
    if (y1_r < min_y) min_y = y1_r;
    if (y1_r > max_y) max_y = y1_r;
    if (y2_r < min_y) min_y = y2_r;
    if (y2_r > max_y) max_y = y2_r;
    if (y3_r < min_y) min_y = y3_r;
    if (y3_r > max_y) max_y = y3_r;

    int x0 = (int)floorf(cx + min_x), x1 = (int)ceilf(cx + max_x) + 1;
    int y0 = (int)floorf(cy + min_y), y1 = (int)ceilf(cy + max_y) + 1;

    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x0 >= x1 || y0 >= y1) return;

    const float inv_det = 1.0f / det;
    const float m00 =  d * inv_det;
    const float m01 = -b * inv_det;
    const float m10 = -c * inv_det;
    const float m11 =  a * inv_det;

    const int32_t step_u_x = (int32_t)lrintf(m00 * 65536.0f);
    const int32_t step_v_x = (int32_t)lrintf(m10 * 65536.0f);
    const int32_t step_u_y = (int32_t)lrintf(m01 * 65536.0f);
    const int32_t step_v_y = (int32_t)lrintf(m11 * 65536.0f);

    const float ox_start = (float)x0 - cx + 0.5f;
    const float oy_start = (float)y0 - cy + 0.5f;

    int32_t row_u = (int32_t)lrintf((ox + m00 * ox_start + m01 * oy_start) * 65536.0f);
    int32_t row_v = (int32_t)lrintf((oy + m10 * ox_start + m11 * oy_start) * 65536.0f);

    for (int py = y0; py < y1; py++, row_u += step_u_y, row_v += step_v_y) {
        int32_t u = row_u, v = row_v;
        uint16_t *dst = g->px + (uint32_t)py * g->stride;
        for (int px = x0; px < x1; px++, u += step_u_x, v += step_v_x) {
            if (u < 0 || v < 0) continue;
            uint32_t lx = (uint32_t)(u >> 16);
            uint32_t ly = (uint32_t)(v >> 16);
            if (lx >= (uint32_t)sw || ly >= (uint32_t)sh) continue;
            uint32_t k = (uint32_t)(sy + (int)ly) * s->w + (uint32_t)(sx + (int)lx);
            draw_pixel_with_alpha(g, &dst[px], s->px[k], s->alpha[k]);
        }
    }
}

void g16_sspr_rot(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                  int dx, int dy, float rz, float scale_x, float scale_y, float ox, float oy)
{
    if (scale_x == 0.0f) scale_x = 1.0f;
    if (scale_y == 0.0f) scale_y = scale_x;
    float c = cosf(rz), sn = sinf(rz);
    g16_sspr_affine(g, s, sx, sy, sw, sh, dx, dy,
                    scale_x * c, -scale_y * sn,
                    scale_x * sn,  scale_y * c,
                    ox, oy);
}

void g16_sspr_alpha(g16_t *g, const g16_sheet_t *s, int sx, int sy, int sw, int sh,
                    int dx, int dy, int flip_x, int flip_y, int global_alpha)
{
    if (!s->px || global_alpha <= 0 || !clamp_rect(s, &sx, &sy, &sw, &sh)) return;
    if (global_alpha > 255) global_alpha = 255;
    dx -= g->cam_x;
    dy -= g->cam_y;
    int x0 = dx, y0 = dy, x1 = dx + sw, y1 = dy + sh;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; y++) {
        int sy_idx = sy + (flip_y ? sh - 1 - (y - dy) : (y - dy));
        const uint32_t sbase = (uint32_t)sy_idx * s->w;
        uint16_t *dst = g->px + (uint32_t)y * g->stride;
        for (int x = x0; x < x1; x++) {
            int sx_idx = sx + (flip_x ? sw - 1 - (x - dx) : (x - dx));
            uint32_t k = sbase + (uint32_t)sx_idx;
            uint8_t a = s->alpha[k];
            if (!a) continue;
            uint32_t eff_a = (a == 1 || a == 255) ? (uint32_t)global_alpha : ((uint32_t)a * (uint32_t)global_alpha + 128) >> 8;
            draw_pixel_with_alpha(g, &dst[x], s->px[k], (uint8_t)eff_a);
        }
    }
}

void g16_spr_alpha(g16_t *g, const g16_sheet_t *s, int n, int x, int y,
                   int wc, int hc, int flip_x, int flip_y, int global_alpha)
{
    if (!s->px || n < 0 || wc <= 0 || hc <= 0) return;
    int per_row = s->w / G16_CELL;
    int sx = (n % per_row) * G16_CELL;
    int sy = (n / per_row) * G16_CELL;
    g16_sspr_alpha(g, s, sx, sy, wc * G16_CELL, hc * G16_CELL, x, y, flip_x, flip_y, global_alpha);
}

void g16_map(g16_t *g, const g16_sheet_t *s, const g16_map_t *m,
             int mx, int my, int x, int y, int mw, int mh)
{
    g16_map_mask(g, s, m, mx, my, x, y, mw, mh, NULL, 0);
}

/* Copy one 8-px row of an opaque tile: 4 words when both ends are 4-byte
 * aligned (the common case), memcpy otherwise. Same bytes either way. */
static inline void copy_row8(uint16_t *dst, const uint16_t *src)
{
    if ((((uintptr_t)dst | (uintptr_t)src) & 3) == 0) {
        g16_w32 *d = (g16_w32 *)dst;
        const g16_w32 *r = (const g16_w32 *)src;
        uint32_t a = r[0], b = r[1], c = r[2], e = r[3];
        d[0] = a; d[1] = b; d[2] = c; d[3] = e;
    } else {
        memcpy(dst, src, G16_CELL * 2);
    }
}

#define TILE_CAP 4096           /* cells packed at most (512 KB) */
#define TILE_RESETS 16          /* a sheet that changes more often than this stays as it is */

/* The packed copy of opaque cell n (64 pixels in a row), or NULL: the caller reads the sheet. */
static const uint16_t *tile_packed(const g16_sheet_t *cs, int n, int ncells)
{
    g16_sheet_t *s = (g16_sheet_t *)cs;     /* (a cache: the pixels it holds are the sheet's) */
    if (s->toff)
        return NULL;
    if (!s->tslot) {
        s->tslot = calloc((size_t)ncells, sizeof *s->tslot);
        s->tpool = aligned_alloc(64, (size_t)TILE_CAP * G16_CELL * G16_CELL * 2);
        s->tcell = malloc((size_t)TILE_CAP * sizeof *s->tcell);
        if (!s->tslot || !s->tpool || !s->tcell) {
            free(s->tslot); free(s->tpool); free(s->tcell);
            s->tslot = NULL; s->tpool = NULL; s->tcell = NULL;
            s->toff = 1;
            return NULL;
        }
        s->tcap = TILE_CAP;
        s->tn = 0;
        s->tver = s->version;
    } else if (s->tver != s->version) {
        for (uint32_t i = 0; i < s->tn; i++)
            s->tslot[s->tcell[i]] = 0;
        s->tn = 0;
        s->tver = s->version;
        if (++s->tresets > TILE_RESETS) {
            free(s->tslot); free(s->tpool); free(s->tcell);
            s->tslot = NULL; s->tpool = NULL; s->tcell = NULL;
            s->toff = 1;
            return NULL;
        }
    }
    unsigned k = s->tslot[n];
    if (!k) {
        if (s->tn >= s->tcap)
            return NULL;
        k = ++s->tn;
        const int per_row = s->w / G16_CELL;
        const uint16_t *src = s->px + (size_t)(n / per_row) * G16_CELL * s->w + (size_t)(n % per_row) * G16_CELL;
        uint16_t *dst = s->tpool + (size_t)(k - 1) * (G16_CELL * G16_CELL);
        for (int r = 0; r < G16_CELL; r++, src += s->w, dst += G16_CELL)
            memcpy(dst, src, G16_CELL * 2);
        s->tcell[k - 1] = (uint32_t)n;
        s->tslot[n] = (uint16_t)k;
    }
    return s->tpool + (size_t)(k - 1) * (G16_CELL * G16_CELL);
}

void g16_map_mask(g16_t *g, const g16_sheet_t *s, const g16_map_t *m,
                  int mx, int my, int x, int y, int mw, int mh, const uint8_t *flags, uint8_t mask)
{
    if (!s->px || !m->cells) return;
    if (!flags) mask = 0;
    const int per_row = s->w / G16_CELL, ncells = per_row * (s->h / G16_CELL);
    /* n / per_row without a divide call: q = n * recip >> 32 is exact for
     * n < 65536 and per_row < 65536 (checked in tests/bm/test_bm.c). */
    const uint32_t recip = per_row > 0 && per_row < 65536 ? 0xFFFFFFFFu / (uint32_t)per_row + 1u : 0;
    x -= g->cam_x;
    y -= g->cam_y;
    for (int cy = 0; cy < mh; cy++) {
        int row = my + cy, py = y + cy * G16_CELL;
        if (row < 0 || row >= m->h || py >= g->cy1 || py + G16_CELL <= g->cy0) continue;
        const uint16_t *crow = m->cells + (size_t)row * m->w;
        const int ydone = py >= g->cy0 && py + G16_CELL <= g->cy1;
        for (int cx = 0; cx < mw; cx++) {
            int col = mx + cx, px = x + cx * G16_CELL;
            if (col < 0 || col >= m->w || px >= g->cx1 || px + G16_CELL <= g->cx0) continue;
            int n = crow[col];
            if (n == 0 || n >= ncells || (mask && !(flags[n] & mask))) continue;
            int ty, tx;
            if (recip) {
                ty = (int)(((uint64_t)(uint32_t)n * recip) >> 32);
                tx = n - ty * per_row;
            } else {
                ty = n / per_row;
                tx = n % per_row;
            }
            tx *= G16_CELL;
            ty *= G16_CELL;
            const int fast_map = (g->blend_mode == G16_BLEND_NORMAL && g->global_alpha == 255 && g->palt_mask == 0x0001 && g->color_key < 0);
            if (fast_map && s->cell_opaque[n] && ydone && px >= g->cx0 && px + G16_CELL <= g->cx1) {
                /* whole tile inside the clip: 8 inline row copies */
                uint16_t *dst = g->px + (size_t)py * g->stride + px;
                const uint16_t *pk = tile_packed(s, n, ncells);
                if (pk) {
                    for (int r = 0; r < G16_CELL; r++, pk += G16_CELL, dst += g->stride)
                        copy_row8(dst, pk);
                } else {
                    const uint16_t *src = s->px + (size_t)ty * s->w + tx;
                    for (int r = 0; r < G16_CELL; r++, src += s->w, dst += g->stride)
                        copy_row8(dst, src);
                }
            } else {
                blit(g, s, tx, ty, G16_CELL, G16_CELL, px, py, 0, 0, s->cell_opaque[n]);
            }
        }
    }
}

int g16_text(g16_t *g, int x, int y, const char *str, uint16_t c)
{
    const font_t *f = g->font;
    int sx = x - g->cam_x, sy = y - g->cam_y;
    const int cw = f->width, wb = (cw + 7) / 8;
    for (; *str; str++, sx += cw, x += cw) {
        if (*str == '\n') { sy += f->height; sx = x = x - cw; continue; }
        if (sx >= g->cx1 || sx + cw <= g->cx0 || sy >= g->cy1 || sy + f->height <= g->cy0)
            continue;
        const uint8_t *gl = f->glyphs + (uint8_t)*str * f->height * wb;
        for (int r = 0; r < f->height; r++) {
            int py = sy + r;
            if (py < g->cy0 || py >= g->cy1) continue;
            for (int k = 0; k < wb; k++) {
                uint8_t bits = gl[r * wb + k];
                if (!bits) continue;
                int bx = sx + k * 8;
                for (int b = 0; b < 8;) {
                    if (!(bits & (0x80 >> b))) { b++; continue; }
                    int e = b;
                    while (e < 8 && (bits & (0x80 >> e))) e++;
                    hline(g, bx + b, bx + e - 1, py, c);
                    b = e;
                }
            }
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
    free(s->tpool);
    free(s->tslot);
    free(s->tcell);
    memset(s, 0, sizeof *s);
}

void g16_sheet_set_alpha(g16_sheet_t *s, int x, int y, uint16_t c, uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    uint32_t i = (uint32_t)y * s->w + x;
    s->px[i] = c;
    s->alpha[i] = alpha;
    s->version++;
    if (alpha != 1 && alpha != 255)
        s->cell_opaque[(y / G16_CELL) * (s->w / G16_CELL) + x / G16_CELL] = 0;
}

void g16_sheet_set(g16_sheet_t *s, int x, int y, uint16_t c, int opaque)
{
    g16_sheet_set_alpha(s, x, y, c, opaque ? 1 : 0);
}

void g16_sheet_update_cell(g16_sheet_t *s, int cx, int cy)
{
    int per_row = s->w / G16_CELL;
    if (cx < 0 || cy < 0 || cx >= per_row || cy >= s->h / G16_CELL) return;
    int all = 1;
    for (int y = 0; y < G16_CELL && all; y++)
        for (int x = 0; x < G16_CELL; x++) {
            uint8_t a = s->alpha[(uint32_t)(cy * G16_CELL + y) * s->w + cx * G16_CELL + x];
            if (a != 1 && a != 255) { all = 0; break; }
        }
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
    /* 64-byte lines: the QPU fade (qpu_fade_apply) reads these arrays by DMA */
    f->lv = aligned_alloc(64, ((size_t)w * h + 63) & ~(size_t)63);
    f->index = aligned_alloc(64, 65536);
    f->tab = aligned_alloc(64, G16_FADE_LEVELS * 256 * sizeof *f->tab);
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
    free(f->lv2);
    free(f->index);
    free(f->tab);
    f->lv = f->lv2 = f->index = NULL;
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

int g16_fade_set_step(g16_fade_t *f, int step)
{
    step = step >= 2 ? 2 : 1;
    if (step == 2) {
        const int w2 = (f->w + 1) >> 1, h2 = (f->h + 1) >> 1;
        if (!f->lv2 || f->w2 != w2 || f->h2 != h2) {
            uint8_t *m = malloc((size_t)w2 * h2);
            if (!m)
                return -1;
            free(f->lv2);
            f->lv2 = m;
            f->w2 = w2;
            f->h2 = h2;
        }
    }
    f->step = step;
    return 0;
}

void g16_fade_clear(g16_fade_t *f, int ambient)
{
    if (ambient < 0) ambient = 0;
    if (ambient >= f->levels) ambient = f->levels - 1;
    if (f->step == 2)
        memset(f->lv2, ambient, (size_t)f->w2 * f->h2);
    else
        memset(f->lv, ambient, (size_t)f->w * f->h);
}

void g16_fade_flush(g16_fade_t *f)
{
    if (f->step != 2)
        return;
    for (int y = 0; y < f->h; y++) {
        uint8_t *dst = f->lv + (size_t)y * f->w;
        if (y & 1) {                    /* the same levels as the row above */
            memcpy(dst, dst - f->w, (size_t)f->w);
            continue;
        }
        const uint8_t *src = f->lv2 + (size_t)(y >> 1) * f->w2;
        if (!(f->w & 1)) {              /* a halfword for each level: half the stores */
            uint16_t *d16 = (uint16_t *)dst;
            for (int i = 0; i < f->w2; i++)
                d16[i] = (uint16_t)(src[i] * 0x0101);
            continue;
        }
        for (int i = 0; i < f->w2; i++) {
            dst[2 * i] = src[i];
            if (2 * i + 1 < f->w)
                dst[2 * i + 1] = src[i];
        }
    }
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
    const int half = f->step == 2;
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
    if (half) {
        /* a level for each 2x2 pixels, taken at the block's middle (2 * bx + 1, 2 * by + 1) */
        for (int by = y0 >> 1; by <= y1 >> 1 && by < f->h2; by++) {
            const int dy = 2 * by + 1 - y;
            const uint32_t dy2 = (uint32_t)(dy * dy);
            if (dy2 >= r2)
                continue;
            const int hw = isqrt32(r2 - dy2);
            int bx0 = (x - hw) >> 1, bx1 = (x + hw - 1) >> 1;
            if (bx0 < 0) bx0 = 0;
            if (bx1 >= f->w2) bx1 = f->w2 - 1;
            uint8_t *row = f->lv2 + (size_t)by * f->w2;
            const int *o = off[by & 3];
            for (int bx = bx0; bx <= bx1; bx++) {
                const int dx = 2 * bx + 1 - x;
                uint32_t t = ((uint32_t)(dx * dx) + dy2) * inv >> 16;
                if (t > 1024) t = 1024;
                int lv = (level * fade_root[t] + o[bx & 3]) >> 8;
                if (lv > row[bx])
                    row[bx] = (uint8_t)lv;
            }
        }
        return;
    }
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
    const int half = f->step == 2;      /* step 2: the level of 2x2 pixels, read from lv2 */
    for (int y = 0; y < h; y++) {
        uint16_t *row = g->px + (uint32_t)y * g->stride;
        const uint8_t *lv = half ? f->lv2 + (size_t)(y >> 1) * f->w2 : f->lv + (size_t)y * f->w;
        for (int x = 0; x < w; x++) {
            const uint16_t c = row[x];
            const int k = lv[x >> half], i = f->index[c];
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

/* ------------------------------------------------------- 2D paths & polygons */

void g16_path_init(g16_path_t *p)
{
    p->pts = NULL;
    p->count = 0;
    p->cap = 0;
    p->cur_x = p->cur_y = 0.0f;
    p->start_x = p->start_y = 0.0f;
    p->has_start = 0;
}

void g16_path_free(g16_path_t *p)
{
    if (p->pts) {
        free(p->pts);
        p->pts = NULL;
    }
    p->count = 0;
    p->cap = 0;
    p->has_start = 0;
}

void g16_path_reset(g16_path_t *p)
{
    p->count = 0;
    p->cur_x = p->cur_y = 0.0f;
    p->start_x = p->start_y = 0.0f;
    p->has_start = 0;
}

static int path_add_pt(g16_path_t *p, float x, float y, uint8_t cmd)
{
    if (p->count >= p->cap) {
        int ncap = p->cap < 64 ? 64 : p->cap * 2;
        g16_path_pt_t *npts = (g16_path_pt_t *)realloc(p->pts, (size_t)ncap * sizeof(g16_path_pt_t));
        if (!npts) return 0;
        p->pts = npts;
        p->cap = ncap;
    }
    p->pts[p->count].x = x;
    p->pts[p->count].y = y;
    p->pts[p->count].cmd = cmd;
    p->count++;
    return 1;
}

void g16_path_move_to(g16_path_t *p, float x, float y)
{
    path_add_pt(p, x, y, G16_PATH_MOVE);
    p->cur_x = x;
    p->cur_y = y;
    p->start_x = x;
    p->start_y = y;
    p->has_start = 1;
}

void g16_path_line_to(g16_path_t *p, float x, float y)
{
    if (!p->has_start)
        g16_path_move_to(p, 0.0f, 0.0f);
    path_add_pt(p, x, y, G16_PATH_LINE);
    p->cur_x = x;
    p->cur_y = y;
}

void g16_path_close(g16_path_t *p)
{
    if (p->has_start && (p->cur_x != p->start_x || p->cur_y != p->start_y)) {
        path_add_pt(p, p->start_x, p->start_y, G16_PATH_CLOSE);
        p->cur_x = p->start_x;
        p->cur_y = p->start_y;
    }
}

static void subdivide_quad(g16_path_t *p, float x0, float y0, float cx, float cy, float x1, float y1, int depth)
{
    float mx = (x0 + x1) * 0.5f;
    float my = (y0 + y1) * 0.5f;
    float dx = cx - mx;
    float dy = cy - my;
    if (depth >= 6 || (dx * dx + dy * dy) <= 0.25f) {
        g16_path_line_to(p, x1, y1);
        return;
    }
    float x01 = (x0 + cx) * 0.5f, y01 = (y0 + cy) * 0.5f;
    float x12 = (cx + x1) * 0.5f, y12 = (cy + y1) * 0.5f;
    float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
    subdivide_quad(p, x0, y0, x01, y01, x012, y012, depth + 1);
    subdivide_quad(p, x012, y012, x12, y12, x1, y1, depth + 1);
}

void g16_path_quad_to(g16_path_t *p, float cx, float cy, float x, float y)
{
    if (!p->has_start)
        g16_path_move_to(p, 0.0f, 0.0f);
    subdivide_quad(p, p->cur_x, p->cur_y, cx, cy, x, y, 0);
}

static void subdivide_cubic(g16_path_t *p, float x0, float y0, float c1x, float c1y,
                            float c2x, float c2y, float x1, float y1, int depth)
{
    float d1x = c1x - (2.0f * x0 + x1) / 3.0f;
    float d1y = c1y - (2.0f * y0 + y1) / 3.0f;
    float d2x = c2x - (x0 + 2.0f * x1) / 3.0f;
    float d2y = c2y - (y0 + 2.0f * y1) / 3.0f;
    float d = (d1x * d1x + d1y * d1y) + (d2x * d2x + d2y * d2y);
    if (depth >= 6 || d <= 0.25f) {
        g16_path_line_to(p, x1, y1);
        return;
    }
    float x01 = (x0 + c1x) * 0.5f, y01 = (y0 + c1y) * 0.5f;
    float x12 = (c1x + c2x) * 0.5f, y12 = (c1y + c2y) * 0.5f;
    float x23 = (c2x + x1) * 0.5f, y23 = (c2y + y1) * 0.5f;

    float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float x0123 = (x012 + x123) * 0.5f, y0123 = (y012 + y123) * 0.5f;

    subdivide_cubic(p, x0, y0, x01, y01, x012, y012, x0123, y0123, depth + 1);
    subdivide_cubic(p, x0123, y0123, x123, y123, x23, y23, x1, y1, depth + 1);
}

void g16_path_cubic_to(g16_path_t *p, float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    if (!p->has_start)
        g16_path_move_to(p, 0.0f, 0.0f);
    subdivide_cubic(p, p->cur_x, p->cur_y, c1x, c1y, c2x, c2y, x, y, 0);
}

void g16_path_arc(g16_path_t *p, float cx, float cy, float r, float a0, float a1)
{
    if (r <= 0.0f) {
        g16_path_line_to(p, cx, cy);
        return;
    }
    float da = a1 - a0;
    float abs_da = da < 0 ? -da : da;
    float dtheta = sqrtf(2.0f / r);
    if (dtheta > 0.35f) dtheta = 0.35f;
    if (dtheta < 0.05f) dtheta = 0.05f;
    int steps = (int)ceilf(abs_da / dtheta);
    if (steps < 4) steps = 4;
    if (steps > 128) steps = 128;

    float start_x = cx + r * cosf(a0);
    float start_y = cy + r * sinf(a0);
    if (!p->has_start)
        g16_path_move_to(p, start_x, start_y);
    else
        g16_path_line_to(p, start_x, start_y);

    for (int i = 1; i <= steps; i++) {
        float a = a0 + da * ((float)i / (float)steps);
        g16_path_line_to(p, cx + r * cosf(a), cy + r * sinf(a));
    }
}

typedef struct {
    float x0, y0;
    float x1, y1;
    float ymin, ymax;
    float dx_dy;
    int dir;
} path_edge_t;

typedef struct {
    float x;
    int dir;
} scan_inter_t;

void g16_path_fill(g16_t *g, const g16_path_t *p, uint16_t c, g16_fill_rule_t rule)
{
    if (!g || !p || p->count < 3) return;

    int max_edges = p->count + 16;
    path_edge_t *edges = (path_edge_t *)malloc((size_t)max_edges * sizeof(path_edge_t));
    if (!edges) return;
    int num_edges = 0;

    float cur_x = 0.0f, cur_y = 0.0f;
    float start_x = 0.0f, start_y = 0.0f;
    int subpath_pts = 0;

    for (int i = 0; i < p->count; i++) {
        float px = p->pts[i].x - (float)g->cam_x;
        float py = p->pts[i].y - (float)g->cam_y;

        if (p->pts[i].cmd == G16_PATH_MOVE) {
            if (subpath_pts >= 2 && (cur_x != start_x || cur_y != start_y)) {
                float y0 = cur_y, y1 = start_y;
                if (fabsf(y1 - y0) >= 1e-4f) {
                    if (num_edges >= max_edges) {
                        max_edges *= 2;
                        path_edge_t *nedges = (path_edge_t *)realloc(edges, (size_t)max_edges * sizeof(path_edge_t));
                        if (!nedges) { free(edges); return; }
                        edges = nedges;
                    }
                    edges[num_edges].x0 = cur_x; edges[num_edges].y0 = y0;
                    edges[num_edges].x1 = start_x; edges[num_edges].y1 = y1;
                    edges[num_edges].ymin = y0 < y1 ? y0 : y1;
                    edges[num_edges].ymax = y0 > y1 ? y0 : y1;
                    edges[num_edges].dir = y0 < y1 ? 1 : -1;
                    edges[num_edges].dx_dy = (start_x - cur_x) / (y1 - y0);
                    num_edges++;
                }
            }
            start_x = px;
            start_y = py;
            cur_x = px;
            cur_y = py;
            subpath_pts = 1;
        } else {
            float y0 = cur_y, y1 = py;
            if (fabsf(y1 - y0) >= 1e-4f) {
                if (num_edges >= max_edges) {
                    max_edges *= 2;
                    path_edge_t *nedges = (path_edge_t *)realloc(edges, (size_t)max_edges * sizeof(path_edge_t));
                    if (!nedges) { free(edges); return; }
                    edges = nedges;
                }
                edges[num_edges].x0 = cur_x; edges[num_edges].y0 = y0;
                edges[num_edges].x1 = px; edges[num_edges].y1 = y1;
                edges[num_edges].ymin = y0 < y1 ? y0 : y1;
                edges[num_edges].ymax = y0 > y1 ? y0 : y1;
                edges[num_edges].dir = y0 < y1 ? 1 : -1;
                edges[num_edges].dx_dy = (px - cur_x) / (y1 - y0);
                num_edges++;
            }
            cur_x = px;
            cur_y = py;
            subpath_pts++;
        }
    }

    if (subpath_pts >= 2 && (cur_x != start_x || cur_y != start_y)) {
        float y0 = cur_y, y1 = start_y;
        if (fabsf(y1 - y0) >= 1e-4f) {
            if (num_edges >= max_edges) {
                max_edges += 8;
                path_edge_t *nedges = (path_edge_t *)realloc(edges, (size_t)max_edges * sizeof(path_edge_t));
                if (!nedges) { free(edges); return; }
                edges = nedges;
            }
            edges[num_edges].x0 = cur_x; edges[num_edges].y0 = y0;
            edges[num_edges].x1 = start_x; edges[num_edges].y1 = y1;
            edges[num_edges].ymin = y0 < y1 ? y0 : y1;
            edges[num_edges].ymax = y0 > y1 ? y0 : y1;
            edges[num_edges].dir = y0 < y1 ? 1 : -1;
            edges[num_edges].dx_dy = (start_x - cur_x) / (y1 - y0);
            num_edges++;
        }
    }

    if (num_edges < 2) {
        free(edges);
        return;
    }

    float b_ymin = edges[0].ymin, b_ymax = edges[0].ymax;
    for (int i = 1; i < num_edges; i++) {
        if (edges[i].ymin < b_ymin) b_ymin = edges[i].ymin;
        if (edges[i].ymax > b_ymax) b_ymax = edges[i].ymax;
    }

    int y_start = (int)floorf(b_ymin);
    int y_end = (int)ceilf(b_ymax);
    if (y_start < g->cy0) y_start = g->cy0;
    if (y_end >= g->cy1) y_end = g->cy1 - 1;
    if (y_start > y_end) {
        free(edges);
        return;
    }

    int inter_cap = 64;
    scan_inter_t *inters = (scan_inter_t *)malloc((size_t)inter_cap * sizeof(scan_inter_t));
    if (!inters) {
        free(edges);
        return;
    }

    for (int y = y_start; y <= y_end; y++) {
        float yc = (float)y + 0.5f;
        int num_inters = 0;

        for (int i = 0; i < num_edges; i++) {
            if (edges[i].ymin <= yc && yc < edges[i].ymax) {
                if (num_inters >= inter_cap) {
                    inter_cap *= 2;
                    scan_inter_t *ninters = (scan_inter_t *)realloc(inters, (size_t)inter_cap * sizeof(scan_inter_t));
                    if (!ninters) break;
                    inters = ninters;
                }
                float x = edges[i].x0 + (yc - edges[i].y0) * edges[i].dx_dy;
                int dir = edges[i].dir;
                int k = num_inters;
                while (k > 0 && inters[k - 1].x > x) {
                    inters[k] = inters[k - 1];
                    k--;
                }
                inters[k].x = x;
                inters[k].dir = dir;
                num_inters++;
            }
        }

        if (num_inters < 2) continue;

        if (rule == G16_FILL_NON_ZERO) {
            int winding = 0;
            float span_start = 0.0f;
            for (int i = 0; i < num_inters; i++) {
                int prev_w = winding;
                winding += inters[i].dir;
                if (prev_w == 0 && winding != 0) {
                    span_start = inters[i].x;
                } else if (prev_w != 0 && winding == 0) {
                    float span_end = inters[i].x;
                    int x0 = (int)ceilf(span_start - 0.5f);
                    int x1 = (int)floorf(span_end - 0.5f);
                    if (x0 <= x1)
                        hline(g, x0, x1, y, c);
                }
            }
        } else {
            for (int i = 0; i + 1 < num_inters; i += 2) {
                float span_start = inters[i].x;
                float span_end = inters[i + 1].x;
                int x0 = (int)ceilf(span_start - 0.5f);
                int x1 = (int)floorf(span_end - 0.5f);
                if (x0 <= x1)
                    hline(g, x0, x1, y, c);
            }
        }
    }

    free(inters);
    free(edges);
}

static void g16_stroke_quad(g16_t *g, float x0, float y0, float x1, float y1,
                            float x2, float y2, float x3, float y3, uint16_t c)
{
    float qx[4] = { x0, x1, x2, x3 };
    float qy[4] = { y0, y1, y2, y3 };
    int min_y = (int)ceilf(fminf(fminf(y0, y1), fminf(y2, y3)) - 0.5f);
    int max_y = (int)floorf(fmaxf(fmaxf(y0, y1), fmaxf(y2, y3)) - 0.5f);
    if (min_y < g->cy0) min_y = g->cy0;
    if (max_y >= g->cy1) max_y = g->cy1 - 1;
    for (int y = min_y; y <= max_y; y++) {
        float py = (float)y + 0.5f;
        float inters[4];
        int n = 0;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            float ey0 = qy[i], ey1 = qy[j];
            if ((ey0 <= py && ey1 > py) || (ey1 <= py && ey0 > py)) {
                float t = (py - ey0) / (ey1 - ey0);
                inters[n++] = qx[i] + t * (qx[j] - qx[i]);
            }
        }
        if (n >= 2) {
            float sx0 = inters[0], sx1 = inters[1];
            if (sx0 > sx1) { float tmp = sx0; sx0 = sx1; sx1 = tmp; }
            int ix0 = (int)ceilf(sx0 - 0.5f);
            int ix1 = (int)floorf(sx1 - 0.5f);
            if (ix0 <= ix1)
                hline(g, ix0, ix1, y, c);
        }
    }
}

void g16_path_stroke(g16_t *g, const g16_path_t *p, uint16_t c, float width)
{
    if (!g || !p || p->count < 2) return;
    if (width <= 1.0f) {
        float cur_x = 0.0f, cur_y = 0.0f;
        for (int i = 0; i < p->count; i++) {
            if (p->pts[i].cmd == G16_PATH_MOVE) {
                cur_x = p->pts[i].x;
                cur_y = p->pts[i].y;
            } else {
                g16_line(g, (int)roundf(cur_x), (int)roundf(cur_y),
                            (int)roundf(p->pts[i].x), (int)roundf(p->pts[i].y), c);
                cur_x = p->pts[i].x;
                cur_y = p->pts[i].y;
            }
        }
        return;
    }

    float hw = width * 0.5f;
    int rad = (int)roundf(hw);
    if (rad < 1) rad = 1;

    float cur_x = 0.0f, cur_y = 0.0f;
    for (int i = 0; i < p->count; i++) {
        if (p->pts[i].cmd == G16_PATH_MOVE) {
            cur_x = p->pts[i].x;
            cur_y = p->pts[i].y;
            g16_circfill(g, (int)roundf(cur_x), (int)roundf(cur_y), rad, c);
        } else {
            float x0 = cur_x, y0 = cur_y;
            float x1 = p->pts[i].x, y1 = p->pts[i].y;
            float dx = x1 - x0, dy = y1 - y0;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 1e-4f) {
                float nx = (-dy / len) * hw;
                float ny = (dx / len) * hw;
                float ax = x0 + nx, ay = y0 + ny;
                float bx = x0 - nx, by = y0 - ny;
                float cx = x1 - nx, cy = y1 - ny;
                float dx_pt = x1 + nx, dy_pt = y1 + ny;

                g16_stroke_quad(g, ax, ay, bx, by, cx, cy, dx_pt, dy_pt, c);
            }
            g16_circfill(g, (int)roundf(x1), (int)roundf(y1), rad, c);
            cur_x = x1;
            cur_y = y1;
        }
    }
}

void g16_poly(g16_t *g, const float *pts, int count, uint16_t c, int closed)
{
    if (!g || count < 2 || !pts) return;
    for (int i = 0; i < count - 1; i++) {
        g16_line(g, (int)roundf(pts[i * 2]), (int)roundf(pts[i * 2 + 1]),
                    (int)roundf(pts[(i + 1) * 2]), (int)roundf(pts[(i + 1) * 2 + 1]), c);
    }
    if (closed && count >= 3) {
        g16_line(g, (int)roundf(pts[(count - 1) * 2]), (int)roundf(pts[(count - 1) * 2 + 1]),
                    (int)roundf(pts[0]), (int)roundf(pts[1]), c);
    }
}

void g16_polyfill(g16_t *g, const float *pts, int count, uint16_t c, g16_fill_rule_t rule)
{
    if (!g || count < 3 || !pts) return;
    g16_path_t p;
    g16_path_init(&p);
    g16_path_move_to(&p, pts[0], pts[1]);
    for (int i = 1; i < count; i++)
        g16_path_line_to(&p, pts[i * 2], pts[i * 2 + 1]);
    g16_path_close(&p);
    g16_path_fill(g, &p, c, rule);
    g16_path_free(&p);
}

/* ------------------------------------------------------- Filters & radial gradients */

void g16_blur(g16_t *g, int x, int y, int w, int h, int radius, int passes)
{
    if (!g || !g->px || radius <= 0 || w <= 0 || h <= 0) return;
    if (radius > 32) radius = 32;
    if (passes < 1) passes = 1;
    if (passes > 4) passes = 4;

    x -= g->cam_x;
    y -= g->cam_y;
    int x1 = x + w, y1 = y + h;
    if (x < g->cx0) x = g->cx0;
    if (y < g->cy0) y = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    int rw = x1 - x;
    int rh = y1 - y;
    if (rw <= 1 || rh <= 1) return;

    uint16_t *tmp = (uint16_t *)malloc((size_t)rw * (size_t)rh * sizeof(uint16_t));
    if (!tmp) return;

    int ksize = 2 * radius + 1;
    uint32_t inv_k = (uint32_t)((1ULL << 24) / (uint32_t)ksize);

    for (int p = 0; p < passes; p++) {
        /* Pass 1: Horizontal blur from g->px into tmp */
        for (int row = 0; row < rh; row++) {
            const uint16_t *src_row = g->px + (uint32_t)(y + row) * g->stride + x;
            uint16_t *dst_row = tmp + (size_t)row * rw;

            uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
            for (int i = -radius; i <= radius; i++) {
                int cx = i < 0 ? 0 : (i >= rw ? rw - 1 : i);
                uint16_t c = src_row[cx];
                sum_r += (c >> 11);
                sum_g += (c >> 5 & 63);
                sum_b += (c & 31);
            }

            for (int col = 0; col < rw; col++) {
                uint32_t r = (sum_r * inv_k + (1 << 23)) >> 24;
                uint32_t g_val = (sum_g * inv_k + (1 << 23)) >> 24;
                uint32_t b = (sum_b * inv_k + (1 << 23)) >> 24;
                if (r > 31) r = 31;
                if (g_val > 63) g_val = 63;
                if (b > 31) b = 31;
                dst_row[col] = (uint16_t)(r << 11 | g_val << 5 | b);

                int left_x = col - radius;
                int right_x = col + radius + 1;
                if (left_x < 0) left_x = 0;
                if (right_x >= rw) right_x = rw - 1;

                uint16_t c_out = src_row[left_x];
                uint16_t c_in = src_row[right_x];

                sum_r += (c_in >> 11) - (c_out >> 11);
                sum_g += (c_in >> 5 & 63) - (c_out >> 5 & 63);
                sum_b += (c_in & 31) - (c_out & 31);
            }
        }

        /* Pass 2: Vertical blur from tmp back into g->px */
        for (int col = 0; col < rw; col++) {
            uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
            for (int i = -radius; i <= radius; i++) {
                int cy = i < 0 ? 0 : (i >= rh ? rh - 1 : i);
                uint16_t c = tmp[(size_t)cy * rw + col];
                sum_r += (c >> 11);
                sum_g += (c >> 5 & 63);
                sum_b += (c & 31);
            }

            for (int row = 0; row < rh; row++) {
                uint32_t r = (sum_r * inv_k + (1 << 23)) >> 24;
                uint32_t g_val = (sum_g * inv_k + (1 << 23)) >> 24;
                uint32_t b = (sum_b * inv_k + (1 << 23)) >> 24;
                if (r > 31) r = 31;
                if (g_val > 63) g_val = 63;
                if (b > 31) b = 31;
                g->px[(uint32_t)(y + row) * g->stride + (x + col)] = (uint16_t)(r << 11 | g_val << 5 | b);

                int top_y = row - radius;
                int bot_y = row + radius + 1;
                if (top_y < 0) top_y = 0;
                if (bot_y >= rh) bot_y = rh - 1;

                uint16_t c_out = tmp[(size_t)top_y * rw + col];
                uint16_t c_in = tmp[(size_t)bot_y * rw + col];

                sum_r += (c_in >> 11) - (c_out >> 11);
                sum_g += (c_in >> 5 & 63) - (c_out >> 5 & 63);
                sum_b += (c_in & 31) - (c_out & 31);
            }
        }
    }

    free(tmp);
}

void g16_conv3(g16_t *g, int x, int y, int w, int h, const int16_t kernel[9], int divisor, int offset)
{
    if (!g || !g->px || !kernel || w <= 0 || h <= 0) return;

    x -= g->cam_x;
    y -= g->cam_y;
    int x1 = x + w, y1 = y + h;
    if (x < g->cx0) x = g->cx0;
    if (y < g->cy0) y = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    int rw = x1 - x, rh = y1 - y;
    if (rw <= 0 || rh <= 0) return;

    if (divisor == 0) {
        int sum = 0;
        for (int i = 0; i < 9; i++) sum += kernel[i];
        divisor = sum == 0 ? 1 : sum;
    }

    uint16_t *tmp = (uint16_t *)malloc((size_t)rw * (size_t)rh * sizeof(uint16_t));
    if (!tmp) return;

    for (int row = 0; row < rh; row++) {
        const uint16_t *src_row = g->px + (uint32_t)(y + row) * g->stride + x;
        memcpy(tmp + (size_t)row * rw, src_row, (size_t)rw * sizeof(uint16_t));
    }

    for (int row = 0; row < rh; row++) {
        uint16_t *dst_row = g->px + (uint32_t)(y + row) * g->stride + x;
        for (int col = 0; col < rw; col++) {
            int32_t r = 0, g_val = 0, b = 0;
            for (int ky = -1; ky <= 1; ky++) {
                int sy = row + ky;
                if (sy < 0) sy = 0; else if (sy >= rh) sy = rh - 1;
                const uint16_t *krow = tmp + (size_t)sy * rw;
                for (int kx = -1; kx <= 1; kx++) {
                    int sx = col + kx;
                    if (sx < 0) sx = 0; else if (sx >= rw) sx = rw - 1;
                    uint16_t c = krow[sx];
                    int16_t w_val = kernel[(ky + 1) * 3 + (kx + 1)];
                    r += (int32_t)(c >> 11) * w_val;
                    g_val += (int32_t)(c >> 5 & 63) * w_val;
                    b += (int32_t)(c & 31) * w_val;
                }
            }
            if (divisor != 1) {
                r /= divisor;
                g_val /= divisor;
                b /= divisor;
            }
            r += offset; g_val += offset; b += offset;
            if (r < 0) r = 0; else if (r > 31) r = 31;
            if (g_val < 0) g_val = 0; else if (g_val > 63) g_val = 63;
            if (b < 0) b = 0; else if (b > 31) b = 31;
            dst_row[col] = (uint16_t)(r << 11 | g_val << 5 | b);
        }
    }

    free(tmp);
}

void g16_radgrad(g16_t *g, int cx, int cy, int r, uint32_t c0, uint32_t c1)
{
    if (!g || !g->px || r <= 0) return;

    uint16_t lut[256];
    uint32_t r0 = (c0 >> 16 & 0xFF) >> 3;
    uint32_t g0 = (c0 >> 8 & 0xFF) >> 2;
    uint32_t b0 = (c0 & 0xFF) >> 3;

    uint32_t r1 = (c1 >> 16 & 0xFF) >> 3;
    uint32_t g1 = (c1 >> 8 & 0xFF) >> 2;
    uint32_t b1 = (c1 & 0xFF) >> 3;

    for (int i = 0; i < 256; i++) {
        uint32_t cr = (r0 * (uint32_t)(255 - i) + r1 * (uint32_t)i + 128) / 255;
        uint32_t cg = (g0 * (uint32_t)(255 - i) + g1 * (uint32_t)i + 128) / 255;
        uint32_t cb = (b0 * (uint32_t)(255 - i) + b1 * (uint32_t)i + 128) / 255;
        lut[i] = (uint16_t)(cr << 11 | cg << 5 | cb);
    }

    cx -= g->cam_x;
    cy -= g->cam_y;

    int y0 = cy - r, y1 = cy + r;
    if (y0 < g->cy0) y0 = g->cy0;
    if (y1 >= g->cy1) y1 = g->cy1 - 1;
    if (y0 > y1) return;

    int64_t r2 = (int64_t)r * r;
    if (r2 <= 0) return;

    for (int y = y0; y <= y1; y++) {
        int64_t dy = y - cy;
        int64_t dy2 = dy * dy;
        if (dy2 > r2) continue;

        int dx_max = (int)sqrtf((float)(r2 - dy2));
        int x_left = cx - dx_max;
        int x_right = cx + dx_max;
        if (x_left < g->cx0) x_left = g->cx0;
        if (x_right >= g->cx1) x_right = g->cx1 - 1;
        if (x_left > x_right) continue;

        uint16_t *dst = g->px + (uint32_t)y * g->stride;
        for (int x = x_left; x <= x_right; x++) {
            int64_t dx = x - cx;
            int64_t d2 = dx * dx + dy2;
            int idx = (int)((d2 * 255) / r2);
            if (idx > 255) idx = 255;
            dst[x] = lut[idx];
        }
    }
}
