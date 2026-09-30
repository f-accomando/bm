#include "menu_ui.h"
#include "icons.h"
#include "b33/b33.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 360
#define FRAME_US 16667

/* layout: text sits on the 8x16 grid, on solid colours (the QEMU tests
 * read the screen back) */
#define BAR_H       48              /* top bar: rows 0-2 */
#define TITLE_ROW   4               /* name of the selected cartridge */
#define CARD_W      128
#define CARD_H      80
#define RADIUS      6               /* rounded corners of the covers */
#define GAP_X       16
#define PITCH_Y     (CARD_H + GAP_X)
#define GRID_X0     ((SW - (MENU_COLS * CARD_W + (MENU_COLS - 1) * GAP_X)) / 2)
#define FOOT_Y      316             /* bottom bar, text rows 20-21 */
#define GRID_BOT    FOOT_Y          /* grid clip */
/* two whole rows, and the top of the next one showing as much as two
 * corner radii: there is more below */
#define PEEK        (2 * RADIUS)
#define GRID_Y0     (GRID_BOT - PEEK - 2 * PITCH_Y)     /* first row of covers */
#define GRID_TOP    (GRID_Y0 - 8)   /* the selection ring is 6 px out */
#define FADE_FRAMES 10

#define C_BAR       0x16161C
#define C_LINE      0x3A3A46
#define C_TEXT      0xF0F0F4
#define C_DIM       0x9A9AA8
#define C_PILL      0x101016
#define C_TAB_ON    0xECECF0
#define C_ACCENT    0x00C8F0        /* selection ring */
#define C_BT        0x1E7BF2        /* the number of a Bluetooth controller */

static g16_t g;
static int ready;
static uint32_t con_w, con_h, t0, deadline;
static float scroll;                /* first visible row, eased */
static int first_row;
static uint16_t *bg_cur, *bg_prev;  /* blurred covers, 640x360 */
static const g16_sheet_t *bg_key;   /* the cover bg_cur was made from */
static int bg_valid, fade;
static int dim;                     /* a panel is open: the rest at half brightness */

/* RGB565 at half brightness */
static inline uint16_t half(uint16_t c) { return (uint16_t)(c >> 1 & 0x7BEF); }

static uint16_t c16(uint32_t rgb) { return g16_rgb24(rgb); }

/* ---------------------------------------------------------------- covers */

int menu_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h)
{
    if (w != B33_COVER_W || h != B33_COVER_H || g16_sheet_alloc(s, w, h) != 0)
        return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
            g16_sheet_set(s, x, y, g16_rgb(p[0], p[1], p[2]), 1);
        }
    return 0;
}

int menu_make_cover(g16_sheet_t *s, const char *title, const char *kind)
{
    if (g16_sheet_alloc(s, B33_COVER_W, B33_COVER_H) != 0)
        return -1;
    uint32_t h = 2166136261u;
    for (const char *p = title; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    g16_t cg;
    g16_target(&cg, s->px, (uint32_t)s->w, s->w, s->h, &font_console_8x16);
    uint32_t r = 40 + (h & 63), gg = 40 + (h >> 6 & 63), b = 70 + (h >> 12 & 95);
    for (int y = 0; y < s->h; y++) {
        uint32_t k = 100 + (uint32_t)y * 2;
        g16_rectfill(&cg, 0, y, s->w, 1, g16_rgb(r * k / 160, gg * k / 160, b * k / 160));
    }
    /* the title on up to three lines of 14 characters, split at spaces */
    char lines[3][15] = { "", "", "" };
    int n = 0;
    const char *p = title;
    while (*p && n < 3) {
        while (*p == ' ') p++;
        int len = (int)strlen(p);
        int take = len <= 14 ? len : 14;
        if (len > 14)
            for (int i = 14; i > 0; i--)
                if (p[i] == ' ') { take = i; break; }
        memcpy(lines[n], p, (size_t)take);
        lines[n][take] = 0;
        n++;
        p += take;
    }
    int y0 = (s->h - n * 16) / 2 - 4;
    for (int i = 0; i < n; i++) {
        int x = (s->w - (int)strlen(lines[i]) * 8) / 2;
        g16_text(&cg, x + 1, y0 + i * 16 + 1, lines[i], 0);
        g16_text(&cg, x, y0 + i * 16, lines[i], g16_rgb(255, 230, 120));
    }
    g16_text(&cg, s->w - 30, s->h - 20, kind, g16_rgb(200, 210, 230));
    memset(s->alpha, 1, (size_t)s->w * s->h);
    return 0;
}

/* ---------------------------------------------------------------- tool covers */

/* filled triangle, for the small icons (edge functions over the box) */
static void tri_fill(g16_t *cg, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (!area)
        return;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            int a = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            int b = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            int d = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if (area > 0 ? (a >= 0 && b >= 0 && d >= 0) : (a <= 0 && b <= 0 && d <= 0))
                g16_pset(cg, x, y, c);
        }
}

static void thick_line(g16_t *cg, int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int d = 0; d < 4; d++)
        g16_line(cg, x0 + (d & 1), y0 + (d >> 1), x1 + (d & 1), y1 + (d >> 1), c);
}

/* the icon, about 44x36 around (cx, cy) */
static void draw_icon(g16_t *cg, int icon, int cx, int cy, uint16_t ink, uint16_t dark)
{
    switch (icon) {
    case MENU_ICON_TERMINAL:                    /* a window with a prompt */
        g16_rectfill(cg, cx - 24, cy - 17, 48, 34, ink);
        g16_rectfill(cg, cx - 22, cy - 11, 44, 26, dark);
        g16_text(cg, cx - 18, cy - 8, ">_", g16_rgb(120, 255, 140));
        break;
    case MENU_ICON_LUA:                         /* a moon and its orbit */
        g16_circfill(cg, cx - 2, cy + 1, 16, ink);
        g16_circfill(cg, cx + 5, cy - 6, 6, dark);
        g16_circ(cg, cx - 2, cy + 1, 21, ink);
        g16_circfill(cg, cx + 17, cy - 14, 4, ink);
        break;
    case MENU_ICON_CHIP:                        /* a processor with its pins */
        for (int i = -12; i <= 12; i += 6) {
            g16_rectfill(cg, cx + i - 1, cy - 20, 3, 40, ink);
            g16_rectfill(cg, cx - 20, cy + i - 1, 40, 3, ink);
        }
        g16_rectfill(cg, cx - 15, cy - 15, 30, 30, dark);
        g16_rect(cg, cx - 15, cy - 15, 30, 30, ink);
        g16_rectfill(cg, cx - 7, cy - 7, 14, 14, ink);
        break;
    case MENU_ICON_LOG:                         /* a page of text */
        g16_rectfill(cg, cx - 15, cy - 19, 30, 38, ink);
        for (int i = 0; i < 6; i++)
            g16_rectfill(cg, cx - 11, cy - 14 + i * 6, i % 3 == 2 ? 14 : 22, 2, dark);
        break;
    case MENU_ICON_PAD:                         /* a game controller */
        g16_circfill(cg, cx - 14, cy + 2, 11, ink);
        g16_circfill(cg, cx + 14, cy + 2, 11, ink);
        g16_rectfill(cg, cx - 14, cy - 9, 28, 20, ink);
        g16_rectfill(cg, cx - 19, cy, 11, 3, dark);
        g16_rectfill(cg, cx - 15, cy - 4, 3, 11, dark);
        g16_circfill(cg, cx + 14, cy - 3, 2, dark);
        g16_circfill(cg, cx + 14, cy + 6, 2, dark);
        g16_circfill(cg, cx + 10, cy + 1, 2, dark);
        g16_circfill(cg, cx + 18, cy + 1, 2, dark);
        break;
    case MENU_ICON_SOUND:                       /* two beamed notes */
        g16_circfill(cg, cx - 12, cy + 12, 6, ink);
        g16_circfill(cg, cx + 12, cy + 8, 6, ink);
        g16_rectfill(cg, cx - 8, cy - 16, 3, 28, ink);
        g16_rectfill(cg, cx + 16, cy - 20, 3, 28, ink);
        for (int i = 0; i < 6; i++)
            g16_line(cg, cx - 8, cy - 16 + i, cx + 18, cy - 20 + i, ink);
        break;
    case MENU_ICON_GAUGE: {                     /* a speed dial */
        for (int a = 0; a <= 180; a += 2) {
            float r = (float)a * 3.14159265f / 180.0f;
            for (int k = 17; k <= 21; k++)
                g16_pset(cg, cx + (int)lroundf(cosf(r) * k), cy + 8 - (int)lroundf(sinf(r) * k),
                         a > 140 ? g16_rgb(255, 90, 80) : ink);
        }
        thick_line(cg, cx, cy + 8, cx + 11, cy - 7, ink);
        g16_circfill(cg, cx, cy + 8, 4, ink);
        break;
    }
    case MENU_ICON_TRIANGLES:                   /* a wireframe mesh, one face lit */
        tri_fill(cg, cx - 4, cy - 18, cx + 20, cy + 14, cx - 20, cy + 14, dark);
        tri_fill(cg, cx - 4, cy - 18, cx + 20, cy + 14, cx + 6, cy + 2, ink);
        g16_line(cg, cx - 4, cy - 18, cx + 20, cy + 14, ink);
        g16_line(cg, cx + 20, cy + 14, cx - 20, cy + 14, ink);
        g16_line(cg, cx - 20, cy + 14, cx - 4, cy - 18, ink);
        g16_line(cg, cx - 20, cy + 14, cx + 6, cy + 2, ink);
        break;
    case MENU_ICON_FLAME: {                     /* many sprites at once */
        static const uint32_t cols[] = { 0xFF5A50, 0xFFC040, 0x60E070, 0x50B0FF, 0xE070FF };
        uint32_t s = 12345;
        for (int i = 0; i < 26; i++) {
            s = s * 1103515245u + 12345u;
            int x = cx - 22 + (int)(s >> 16) % 40, y = cy - 18 + (int)(s >> 8 & 255) % 32;
            g16_rectfill(cg, x, y, 5, 5, g16_rgb24(cols[i % 5]));
        }
        break;
    }
    case MENU_ICON_ARROWS:                      /* a copy one way and back */
        g16_rectfill(cg, cx - 20, cy - 10, 30, 5, ink);
        tri_fill(cg, cx + 10, cy - 17, cx + 22, cy - 8, cx + 10, cy + 1, ink);
        g16_rectfill(cg, cx - 10, cy + 8, 30, 5, ink);
        tri_fill(cg, cx - 10, cy + 1, cx - 22, cy + 10, cx - 10, cy + 19, ink);
        break;
    case MENU_ICON_PLAY:                        /* a play button */
        g16_circfill(cg, cx, cy, 19, ink);
        tri_fill(cg, cx - 6, cy - 11, cx + 12, cy, cx - 6, cy + 11, dark);
        break;
    case MENU_ICON_BARS: {                      /* colour bars */
        static const uint32_t cols[] = { 0xC0C0C0, 0xC0C000, 0x00C0C0, 0x00C000,
                                         0xC000C0, 0xC00000, 0x0000C0 };
        for (int i = 0; i < 7; i++)
            g16_rectfill(cg, cx - 21 + i * 6, cy - 17, 6, 34, g16_rgb24(cols[i]));
        g16_rect(cg, cx - 22, cy - 18, 44, 36, ink);
        break;
    }
    case MENU_ICON_CHECK:                       /* a tick in a ring */
        g16_circ(cg, cx, cy, 19, ink);
        g16_circ(cg, cx, cy, 18, ink);
        thick_line(cg, cx - 10, cy, cx - 3, cy + 8, ink);
        thick_line(cg, cx - 3, cy + 8, cx + 11, cy - 9, ink);
        break;
    }
}

int menu_make_tool_cover(g16_sheet_t *s, const char *title, int icon, uint32_t rgb)
{
    if (g16_sheet_alloc(s, B33_COVER_W, B33_COVER_H) != 0)
        return -1;
    g16_t cg;
    g16_target(&cg, s->px, (uint32_t)s->w, s->w, s->h, &font_console_8x16);
    uint32_t r = rgb >> 16, gg = rgb >> 8 & 255, b = rgb & 255;
    for (int y = 0; y < s->h; y++) {                /* lighter at the top */
        uint32_t k = 190 - (uint32_t)y;
        g16_rectfill(&cg, 0, y, s->w, 1, g16_rgb(r * k / 160 > 255 ? 255 : r * k / 160,
                                                  gg * k / 160 > 255 ? 255 : gg * k / 160,
                                                  b * k / 160 > 255 ? 255 : b * k / 160));
    }
    draw_icon(&cg, icon, s->w / 2, 30, g16_rgb(245, 245, 250), g16_rgb(r / 3, gg / 3, b / 3));
    int x = (s->w - (int)strlen(title) * 8) / 2;
    g16_text(&cg, x + 1, 59, title, 0);
    g16_text(&cg, x, 58, title, g16_rgb(255, 255, 255));
    memset(s->alpha, 1, (size_t)s->w * s->h);
    return 0;
}

/* ---------------------------------------------------------------- background */

static const uint8_t bayer[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

/* The cover shrunk to 16x10 (8x8 averages: the blur), stretched back to
 * the whole screen with bilinear filtering, darkened towards the bottom,
 * dithered to RGB565. Made once per selection. */
static void make_background(uint16_t *out, const g16_sheet_t *cover)
{
    enum { LW = 16, LH = 10 };
    float lo[LH][LW][3];
    for (int j = 0; j < LH; j++)
        for (int i = 0; i < LW; i++) {
            uint32_t r = 0, gg = 0, b = 0;
            if (cover && cover->px && cover->w >= 128 && cover->h >= 80) {
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++) {
                        uint32_t c = g16_to_rgb24(cover->px[(j * 8 + y) * cover->w + i * 8 + x]);
                        r += c >> 16; gg += c >> 8 & 255; b += c & 255;
                    }
                r /= 64; gg /= 64; b /= 64;
            } else {
                r = 40; gg = 44; b = 70;
            }
            lo[j][i][0] = (float)r; lo[j][i][1] = (float)gg; lo[j][i][2] = (float)b;
        }
    for (int y = 0; y < SH; y++) {
        float v = (y + 0.5f) * LH / SH - 0.5f;
        int j0 = (int)floorf(v);
        float fy = v - j0;
        int ja = j0 < 0 ? 0 : j0, jb = j0 + 1 >= LH ? LH - 1 : j0 + 1;
        /* darker at the bottom, where the grid is */
        float dark = 0.50f - 0.22f * (float)y / SH;
        for (int x = 0; x < SW; x++) {
            float u = (x + 0.5f) * LW / SW - 0.5f;
            int i0 = (int)floorf(u);
            float fx = u - i0;
            int ia = i0 < 0 ? 0 : i0, ib = i0 + 1 >= LW ? LW - 1 : i0 + 1;
            float edge = 1.0f - 0.25f * fabsf((float)x / SW - 0.5f) * 2.0f;
            int d = bayer[y & 3][x & 3];
            uint32_t c[3];
            for (int k = 0; k < 3; k++) {
                float top = lo[ja][ia][k] + (lo[ja][ib][k] - lo[ja][ia][k]) * fx;
                float bot = lo[jb][ia][k] + (lo[jb][ib][k] - lo[jb][ia][k]) * fx;
                float val = (top + (bot - top) * fy) * dark * edge + 10.0f;
                int q = (int)val + (k == 1 ? d >> 2 : d >> 1);
                c[k] = q < 0 ? 0 : q > 255 ? 255 : (uint32_t)q;
            }
            out[y * SW + x] = g16_rgb(c[0], c[1], c[2]);
        }
    }
}

/* the background into the frame, cross-fading from the previous one */
static void put_background(void)
{
    if (fade > 0) {
        uint32_t a = (uint32_t)(FADE_FRAMES - fade) * 32 / FADE_FRAMES;     /* 0..32 of the new */
        for (int y = 0; y < SH; y++) {
            const uint16_t *p = bg_prev + y * SW, *q = bg_cur + y * SW;
            uint16_t *o = g.px + (uint32_t)y * g.stride;
            for (int x = 0; x < SW; x++) {
                /* 565 blend in one multiply: green in the high half */
                uint32_t A = (p[x] | (uint32_t)p[x] << 16) & 0x07E0F81Fu;
                uint32_t B = (q[x] | (uint32_t)q[x] << 16) & 0x07E0F81Fu;
                uint32_t m = (A * (32 - a) + B * a) >> 5 & 0x07E0F81Fu;
                o[x] = dim ? half((uint16_t)(m | m >> 16)) : (uint16_t)(m | m >> 16);
            }
        }
        fade--;
        return;
    }
    /* the video memory has no cache: only written, never read back */
    for (int y = 0; y < SH; y++) {
        uint16_t *o = g.px + (uint32_t)y * g.stride;
        const uint16_t *q = bg_cur + y * SW;
        if (!dim || y < BAR_H || y >= FOOT_Y)
            memcpy(o, q, SW * 2);
        else
            for (int x = 0; x < SW; x++)
                o[x] = half(q[x]);
    }
}

/* ---------------------------------------------------------------- shapes */

/* columns cut off at row `dy` of a rounded corner of radius r (dy from the
 * edge, 0 = outermost row) */
static int corner_inset(int r, int dy)
{
    if (dy >= r)
        return 0;
    float d = (float)r - dy - 0.5f;
    return r - (int)(sqrtf((float)(r * r) - d * d) + 0.5f);
}

static void hspan(int x0, int x1, int y, uint16_t c)       /* [x0, x1), clipped */
{
    if (y < g.cy0 || y >= g.cy1) return;
    if (x0 < 0) x0 = 0;
    if (x1 > SW) x1 = SW;
    uint16_t *p = g.px + (uint32_t)y * g.stride;
    for (int x = x0; x < x1; x++)
        p[x] = c;
}

static void round_rect(int x, int y, int w, int h, int r, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        int dy = j < h / 2 ? j : h - 1 - j;
        int in = corner_inset(r, dy);
        hspan(x + in, x + w - in, y + j, c);
    }
}

/* ring between two rounded rectangles: outer (x, y, w, h, r), thickness t */
static void round_ring(int x, int y, int w, int h, int r, int t, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        int dy = j < h / 2 ? j : h - 1 - j;
        int in = corner_inset(r, dy);
        int jy = j - t;
        if (jy < 0 || jy >= h - 2 * t) {
            hspan(x + in, x + w - in, y + j, c);
            continue;
        }
        int dyi = jy < (h - 2 * t) / 2 ? jy : h - 2 * t - 1 - jy;
        int ini = corner_inset(r - t, dyi);
        hspan(x + in, x + t + ini, y + j, c);
        hspan(x + w - t - ini, x + w - in, y + j, c);
    }
}

/* the cover with rounded corners, clipped to the grid rows */
static void card(const g16_sheet_t *s, int x, int y)
{
    for (int j = 0; j < CARD_H; j++) {
        int yy = y + j;
        if (yy < g.cy0 || yy >= g.cy1)
            continue;
        int dy = j < CARD_H / 2 ? j : CARD_H - 1 - j;
        int in = corner_inset(RADIUS, dy);
        uint16_t *dst = g.px + (uint32_t)yy * g.stride + x + in;
        if (s && s->px && s->w >= CARD_W && s->h >= CARD_H && !dim) {
            memcpy(dst, s->px + j * s->w + in, (size_t)(CARD_W - 2 * in) * 2);
        } else if (s && s->px && s->w >= CARD_W && s->h >= CARD_H) {
            const uint16_t *src = s->px + j * s->w + in;
            for (int i = 0; i < CARD_W - 2 * in; i++)
                dst[i] = half(src[i]);
        } else {
            for (int i = 0; i < CARD_W - 2 * in; i++)
                dst[i] = dim ? half(c16(0x303040)) : c16(0x303040);
        }
    }
}

/* text on a pill-shaped background, starting at text column `col` */
static int pill_text(int col, int row, const char *s, uint32_t fg, uint32_t bg)
{
    int n = (int)strlen(s);
    round_rect(col * 8 - 8, row * 16 - 4, (n + 2) * 8, 24, 12, c16(bg));
    g16_text(&g, col * 8, row * 16, s, c16(fg));
    return col + n + 2;
}

/* a round button icon with its letter (A, B, X, Y), then a label */
static int hint(int col, int row, const char *btn, const char *label)
{
    int cx = col * 8 + 4, cy = row * 16 + 8;
    for (int dy = -7; dy <= 7; dy++) {
        int w = (int)sqrtf(49.0f - (float)(dy * dy) + 0.5f);
        hspan(cx - w, cx + w + 1, cy + dy, c16(C_TEXT));
    }
    char b[2] = { btn[0], 0 };
    g16_text(&g, col * 8, row * 16, b, c16(C_BAR));
    g16_text(&g, (col + 2) * 8, row * 16, label, c16(C_TEXT));
    return col + 2 + (int)strlen(label) + 3;
}

/* a gear of radius ~7 around (cx, cy), with a hole the colour of `bg` */
static void gear(int cx, int cy, uint16_t c, uint16_t bg)
{
    g16_circfill(&g, cx, cy, 5, c);
    for (int k = 0; k < 8; k++) {
        float a = (float)k * 3.14159265f / 4.0f;
        int x = cx + (int)lroundf(cosf(a) * 6.0f), y = cy + (int)lroundf(sinf(a) * 6.0f);
        g16_rectfill(&g, x - 1, y - 1, 3, 3, c);
    }
    g16_circfill(&g, cx, cy, 2, bg);
}

/* a small triangle pointing up (dir -1) or down (+1), for the scroll marks */
static void scroll_mark(int cx, int cy, int dir, uint16_t c)
{
    for (int i = 0; i < 5; i++)
        hspan(cx - i, cx + i + 1, cy - dir * (i - 2), c);
}

/* panel geometry: text rows 4 (title), 6..16 every other one (the rows),
 * 18 (help) */
#define PANEL_X     (10 * 8)
#define PANEL_W     (60 * 8)
#define PANEL_Y     52
#define PANEL_H     (310 - PANEL_Y)
#define PANEL_ROW0  6
#define LABEL_COL   13
#define VALUE_END   67                          /* values end before this column */

/* faded: a question is over it */
static void draw_panel(const menu_panel_t *p, int faded)
{
    round_rect(PANEL_X - 2, PANEL_Y - 2, PANEL_W + 4, PANEL_H + 4, 16, c16(C_LINE));
    round_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, c16(C_BAR));
    char buf[72];
    ksnprintf(buf, sizeof buf, "%s", p->title ? p->title : "");
    buf[52] = 0;
    g16_text(&g, 12 * 8, 4 * 16, buf, c16(C_TEXT));
    g16_rectfill(&g, PANEL_X + 16, 88, PANEL_W - 32, 1, c16(C_LINE));
    for (int i = 0; i < MENU_PANEL_ROWS && p->top + i < p->n; i++) {
        const menu_row_t *r = &p->rows[p->top + i];
        int row = PANEL_ROW0 + 2 * i, y = row * 16, sel = p->top + i == p->sel;
        if (sel)
            round_rect(PANEL_X + 16, y - 6, PANEL_W - 32, 28, 8, faded ? c16(C_LINE) : c16(C_TAB_ON));
        sel = sel && !faded;
        uint16_t fg = sel ? c16(C_BAR) : r->kind == MENU_ROW_INFO ? c16(C_DIM) : c16(C_TEXT);
        uint16_t fv = sel ? c16(0x4A4A56) : r->kind == MENU_ROW_INFO ? c16(C_TEXT) : c16(C_DIM);
        ksnprintf(buf, sizeof buf, "%s", r->label ? r->label : "");
        buf[30] = 0;
        g16_text(&g, LABEL_COL * 8, y, buf, fg);
        const char *val = r->value ? r->value : "";
        if (r->kind == MENU_ROW_CHOICE && sel)
            ksnprintf(buf, sizeof buf, "< %s >", val);
        else if (r->kind == MENU_ROW_SUB)
            ksnprintf(buf, sizeof buf, "%s%s>", val, val[0] ? "  " : "");
        else
            ksnprintf(buf, sizeof buf, "%s", val);
        int max = VALUE_END - LABEL_COL - (int)strlen(r->label ? r->label : "") - 2;
        if (max > 34) max = 34;
        if (max < 0) max = 0;
        if ((int)strlen(buf) > max)
            buf[max] = 0;
        g16_text(&g, (VALUE_END - (int)strlen(buf)) * 8, y, buf, fv);
    }
    if (p->top > 0)
        scroll_mark(PANEL_X + PANEL_W - 12, 100, -1, c16(C_DIM));
    if (p->top + MENU_PANEL_ROWS < p->n)
        scroll_mark(PANEL_X + PANEL_W - 12, 268, 1, c16(C_DIM));
    g16_rectfill(&g, PANEL_X + 16, 281, PANEL_W - 32, 1, c16(C_LINE));
    if (p->help) {
        ksnprintf(buf, sizeof buf, "%s", p->help);
        buf[56] = 0;
        g16_text(&g, 12 * 8, 18 * 16, buf, c16(C_DIM));
    }
}

/* colour c over b by a (0..255), 0xRRGGBB */
static uint32_t over(uint32_t b, uint32_t c, int a)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int x = (int)(b >> sh & 255), y = (int)(c >> sh & 255);
        out |= (uint32_t)(x + (y - x) * a / 255) << sh;
    }
    return out;
}

/* an icon of icons.c over the bar: the icon in `ink`, the number's disc in
 * `disc` with the digit in `digit`; only written */
static void put_icon(const icon_mask_t *m, int x0, int y0, uint32_t ink, uint32_t disc, uint32_t digit)
{
    for (int y = 0; y < ICON_BH; y++) {
        uint16_t *p = g.px + (uint32_t)(y0 + y) * g.stride + x0;
        for (int x = 0; x < ICON_W; x++) {
            int i = y * ICON_W + x;
            if (!m->icon[i] && !m->disc[i])
                continue;
            uint32_t c = m->digit[i] ? digit : over(over(C_BAR, ink, m->icon[i]), disc, m->disc[i]);
            p[x] = c16(c);
        }
    }
}

/* right-aligned: a keyboard or a controller with its number for each
 * player, then WiFi or Ethernet when the console is on a network */
static void status_icons(const menu_view_t *v)
{
    int icon[5], num[5], bt[5], n = 0;
    for (int p = 0; p < 4; p++)
        if (v->dev[p] != MENU_DEV_NONE) {
            icon[n] = v->dev[p] == MENU_DEV_KEYBOARD ? ICON_KEYBOARD : ICON_PAD;
            bt[n] = v->bt >> p & 1;
            num[n++] = p + 1;
        }
    int players = n;
    if (v->net != MENU_NET_NONE) {
        icon[n] = v->net == MENU_NET_ETHERNET ? ICON_ETHERNET : ICON_WIFI;
        bt[n] = 0;
        num[n++] = 0;
    }
    const int gap = 8, net_gap = 16, y0 = 12;
    int w = n * ICON_W + (n > 1 ? (n - 1) * gap : 0) + (players && players < n ? net_gap - gap : 0);
    int x = SW - 16 - w;
    for (int i = 0; i < n; i++) {
        const icon_mask_t *m = icon_mask(icon[i], num[i]);
        if (m)                      /* white number for USB, blue for Bluetooth */
            put_icon(m, x, y0, i >= players && v->net == MENU_NET_WIFI_WAIT ? C_DIM : C_TEXT,
                     bt[i] ? C_BT : C_TEXT, bt[i] ? C_TEXT : C_BAR);
        x += ICON_W + (i + 1 == players ? net_gap : gap);
    }
}

/* ---------------------------------------------------------------- screen */

int menu_ui_open(framebuffer_t *fb)
{
    if (!bg_cur) {
        bg_cur = malloc(SW * SH * 2);
        bg_prev = malloc(SW * SH * 2);
        if (!bg_cur || !bg_prev) {
            free(bg_cur); free(bg_prev);
            bg_cur = bg_prev = NULL;
            return -1;
        }
    }
    con_w = fb->width;
    con_h = fb->height;
    console_suspend(1);
    if (fb_init_depth(fb, SW, SH, 3, 16) != 0) {
        fb_init(fb, con_w, con_h, 2);
        console_suspend(0);
        return -1;
    }
    ready = 1;
    bg_valid = 0;
    fade = 0;
    t0 = timer_ticks();
    deadline = t0 + FRAME_US;
    return 0;
}

void menu_ui_close(framebuffer_t *fb)
{
    if (!ready)
        return;
    ready = 0;
    fb_init(fb, con_w, con_h, 2);
    console_suspend(0);
}

static uint16_t pulse(float t)
{
    /* the selection ring breathes between two blues */
    float k = 0.5f + 0.5f * sinf(t * 4.0f);
    uint32_t r = (uint32_t)(0x00 + k * 0x70), gg = (uint32_t)(0xB0 + k * 0x40), b = (uint32_t)(0xE8 + k * 0x17);
    return g16_rgb(r, gg, b);
}

void menu_ui_frame(framebuffer_t *fb, const menu_view_t *v)
{
    if (!ready)
        return;
    g16_target(&g, (uint16_t *)fb->base, fb->pitch / 2, SW, SH, &font_console_8x16);
    float t = (float)(timer_ticks() - t0) * 1e-6f;
    const menu_item_t *cur = v->sel >= 0 && v->sel < v->n ? &v->items[v->sel] : NULL;
    dim = v->panel != NULL;

    /* background: the selected cover, blurred; a cross-fade on change */
    const g16_sheet_t *key = cur ? cur->cover : NULL;
    if (!bg_valid || key != bg_key) {
        if (bg_valid) {
            uint16_t *s = bg_prev; bg_prev = bg_cur; bg_cur = s;
            fade = FADE_FRAMES;
        }
        make_background(bg_cur, key);
        bg_key = key;
        bg_valid = 1;
    }
    put_background();

    /* grid: keep the selected row among the two fully visible ones */
    int sel_row = v->sel / MENU_COLS;
    if (sel_row < first_row) first_row = sel_row;
    if (sel_row > first_row + 1) first_row = sel_row - 1;
    if (first_row < 0) first_row = 0;
    scroll += ((float)first_row - scroll) * 0.25f;
    if (fabsf(scroll - (float)first_row) < 0.01f) scroll = (float)first_row;

    g16_clip(&g, 0, GRID_TOP, SW, GRID_BOT - GRID_TOP);
    for (int i = 0; i < v->n; i++) {
        int row = i / MENU_COLS, col = i % MENU_COLS;
        int x = GRID_X0 + col * (CARD_W + GAP_X);
        int y = GRID_Y0 + (int)lroundf(((float)row - scroll) * PITCH_Y);
        if (y + CARD_H + 8 < GRID_TOP || y - 8 >= GRID_BOT)
            continue;
        if (i == v->sel)            /* ring with a gap, like the home screens */
            round_ring(x - 6, y - 6, CARD_W + 12, CARD_H + 12, RADIUS + 6, 3,
                       dim ? c16(0x45454B) : v->on_tabs || v->on_gear ? c16(0x8A8A96) : pulse(t));
        card(v->items[i].cover, x, y);
        if (v->items[i].running) {
            /* on the 8x16 text grid, in the lower part of the cover */
            int tx = (x + 8 + 7) / 8 * 8, ty = (y + CARD_H - 20) / 16 * 16;
            if (ty >= GRID_TOP && ty + 16 <= GRID_BOT) {
                round_rect(tx - 6, ty - 2, 7 * 8 + 12, 20, 10, c16(0x101014));
                g16_text(&g, tx, ty, "Playing", dim ? c16(C_DIM) : c16(C_TEXT));
            }
        }
    }
    g16_clip(&g, 0, 0, 0, 0);

    /* top bar: tabs, settings, then the players and the network */
    g16_rectfill(&g, 0, 0, SW, BAR_H, c16(C_BAR));
    int col = 3;
    for (int i = 0; i < v->ntabs; i++) {
        int n = (int)strlen(v->tabs[i]);
        if (i == v->tab) {
            if (v->on_tabs)
                round_ring(col * 8 - 12, 16 - 8, (n + 2) * 8 + 8, 32, 16, 2, pulse(t));
            pill_text(col, 1, v->tabs[i], C_BAR, C_TAB_ON);
        } else {
            g16_text(&g, col * 8, 16, v->tabs[i], c16(C_DIM));
        }
        col += n + 4;
    }
    /* the settings button, after the tabs */
    col += 1;
    if (v->on_gear)
        round_ring(col * 8 - 12, 16 - 8, 13 * 8 + 8, 32, 16, 2, pulse(t));
    if (v->on_gear)
        round_rect(col * 8 - 8, 16 - 4, 13 * 8, 24, 12, c16(C_TAB_ON));
    gear(col * 8 + 7, 16 + 8, v->on_gear ? c16(C_BAR) : c16(C_DIM), v->on_gear ? c16(C_TAB_ON) : c16(C_BAR));
    g16_text(&g, (col + 3) * 8, 16, "Settings", v->on_gear ? c16(C_BAR) : c16(C_DIM));
    status_icons(v);

    /* the name of the selected cartridge, on a pill */
    if (v->panel) {
        /* the panel covers it */
    } else if (cur && cur->title && cur->title[0]) {
        char buf[72];
        ksnprintf(buf, sizeof buf, "%s", cur->title);
        int n = (int)strlen(buf);
        round_rect(3 * 8 - 10, TITLE_ROW * 16 - 6, (n + 2) * 8 + 4, 28, 14, c16(C_LINE));
        pill_text(3, TITLE_ROW, buf, C_TEXT, C_PILL);
    } else if (v->n == 0) {
        pill_text(3, TITLE_ROW, "nothing here yet", C_DIM, C_PILL);
    }

    /* bottom bar: details, last game, buttons */
    g16_rectfill(&g, 0, FOOT_Y, SW, SH - FOOT_Y, c16(C_BAR));
    g16_rectfill(&g, 16, FOOT_Y + 1, SW - 32, 1, c16(C_LINE));
    char buf[96];
    if (v->details && !v->panel) {
        ksnprintf(buf, sizeof buf, "%s", v->details);
        buf[76] = 0;
        g16_text(&g, 2 * 8, 20 * 16, buf, c16(C_DIM));
    }
    if (v->note && v->note[0]) {
        ksnprintf(buf, sizeof buf, "%s", v->note);
        buf[30] = 0;
        g16_text(&g, 2 * 8, 21 * 16, buf, c16(C_DIM));
    }
    if (v->panel) {
        const menu_row_t *r = v->panel->sel < v->panel->n ? &v->panel->rows[v->panel->sel] : NULL;
        col = 40;
        if (r && r->kind == MENU_ROW_CHOICE) {
            g16_text(&g, col * 8, 21 * 16, "< >", c16(C_TEXT));
            g16_text(&g, (col + 4) * 8, 21 * 16, "Change", c16(C_TEXT));
            col += 13;
        } else if (r && r->kind != MENU_ROW_INFO) {
            col = hint(col, 21, "A", r->kind == MENU_ROW_SUB ? "Open" : "Select");
        }
        hint(col < 55 ? 55 : col, 21, "B", "Back");
    } else if (v->on_gear) {
        col = hint(34, 21, "A", "Settings");
        g16_text(&g, col * 8, 21 * 16, "Start+Select Monitor", c16(C_TEXT));
    } else {
        col = hint(34, 21, "A", cur && cur->kind && strcmp(cur->kind, "tool") == 0 ? "Open" : "Play");
        if (v->n && v->items[v->sel].path && v->items[v->sel].path[0])
            col = hint(col, 21, "X", "Options");
        g16_text(&g, col * 8, 21 * 16, "Start+Select Monitor", c16(C_TEXT));
    }

    if (v->panel)
        draw_panel(v->panel, v->ask != NULL);

    /* a question in a panel over everything */
    if (v->ask) {
        const int px = 12 * 8, py = 8 * 16 - 8, pw = SW - 24 * 8, ph = 5 * 16 + 16;
        round_rect(px - 2, py - 2, pw + 4, ph + 4, 14, c16(C_LINE));
        round_rect(px, py, pw, ph, 12, c16(C_BAR));
        char q[64];
        ksnprintf(q, sizeof q, "%s", v->ask);
        g16_text(&g, (SW / 8 - (int)strlen(q)) / 2 * 8, 9 * 16, q, c16(C_TEXT));
        if (v->ask_detail) {
            ksnprintf(q, sizeof q, "%s", v->ask_detail);
            g16_text(&g, (SW / 8 - (int)strlen(q)) / 2 * 8, 10 * 16, q, c16(C_DIM));
        }
        int c = hint(26, 12, "A", v->ask_yes ? v->ask_yes : "Close it");
        hint(c + 2, 12, "B", "Cancel");
    }

    fb_flip(fb);
    while ((int32_t)(timer_ticks() - deadline) < 0)
        ;
    uint32_t now = timer_ticks();
    deadline += FRAME_US;
    if ((int32_t)(now - deadline) > 0)
        deadline = now + FRAME_US;
}
