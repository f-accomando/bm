#include "menu_ui.h"
#include "config.h"
#include "icons.h"
#include "pointer.h"
#include "prompts.h"
#include "syskeys.h"
#include "bm/bm.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#ifdef BM_RGB30
#include "rgb30/display.h"
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The layout's size (menu_ui_open): 640x360 on the Pi, 360x360 on the
 * RGB30 (shown 2x on its 720x720 panel). The height is the same, so only
 * the columns change: covers per row, the panel, the footer. */
static int sw = 640, sh = 360, cols = MENU_COLS;
#define SW sw
#define SH sh
#define MAX_W 640
#define MAX_H 360
#define WIDE (SW >= 640)                /* the Pi's layout; else the narrow one */
/* the screen is the layout `scale` times bigger (menu_scale=3 on the Pi:
 * 1920x1080), drawn in `frame` and enlarged at the flip */
static int scale = 1;
static uint16_t *frame;
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
#define GRID_X0     ((SW - (cols * CARD_W + (cols - 1) * GAP_X)) / 2)
#define FOOT_Y      316             /* bottom bar, text rows 20-21 */
#define GRID_BOT    FOOT_Y          /* grid clip */
/* two whole rows, and the top of the next one showing as much as two
 * corner radii: there is more below */
#define PEEK        (2 * RADIUS)
#define GRID_Y0     (GRID_BOT - PEEK - 2 * PITCH_Y)     /* first row of covers */
#define GRID_TOP    (GRID_Y0 - 8)   /* the selection ring is 6 px out */
/* scroll bar in the right margin, along the two whole rows */
#define SBAR_W      4
#define SBAR_X      (SW - GRID_X0 / 2 - SBAR_W / 2)
#define SBAR_H      (2 * PITCH_Y - GAP_X)
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
static float tab_shift;             /* the tabs' x, eased: < 0 while the first one hides */
static int first_row;
static uint16_t *bg_cur, *bg_prev;  /* blurred covers, SW x SH */
static const g16_sheet_t *bg_key;   /* the cover bg_cur was made from */
static int bg_valid, fade;
static int dim;                     /* a panel is open: the rest at half brightness */

/* the things the pointer can click in the frame on screen, drawn last on
 * top (menu_ui_hit) */
#define MAX_ZONES 64
static struct zone { int16_t x, y, w, h; uint8_t kind, full; int16_t index; } zones[MAX_ZONES];
static int nzones;

static int hints_dead;              /* the footer's hints are under a question */

static void zone(int x, int y, int w, int h, int kind, int index, int full)
{
    if (nzones < MAX_ZONES && w > 0 && h > 0)
        zones[nzones++] = (struct zone){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                                         (uint8_t)kind, (uint8_t)full, (int16_t)index };
}

menu_hit_t menu_ui_hit(int x, int y)
{
    for (int i = nzones - 1; i >= 0; i--) {
        const struct zone *z = &zones[i];
        if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h)
            return (menu_hit_t){ z->kind, z->index, z->full };
    }
    return (menu_hit_t){ MENU_HIT_NONE, 0, 0 };
}

/* RGB565 at half brightness */
static inline uint16_t half(uint16_t c) { return (uint16_t)(c >> 1 & 0x7BEF); }

static uint16_t c16(uint32_t rgb) { return g16_rgb24(rgb); }

/* ---------------------------------------------------------------- covers */

int menu_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h)
{
    if (w != BM_COVER_W || h != BM_COVER_H || g16_sheet_alloc(s, w, h) != 0)
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
    if (g16_sheet_alloc(s, BM_COVER_W, BM_COVER_H) != 0)
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
    case MENU_ICON_ASSIST:                      /* a speech bubble with a question */
        g16_rectfill(cg, cx - 24, cy - 12, 48, 24, ink);
        g16_rectfill(cg, cx - 18, cy - 18, 36, 36, ink);
        g16_circfill(cg, cx - 18, cy - 12, 6, ink);
        g16_circfill(cg, cx + 17, cy - 12, 6, ink);
        g16_circfill(cg, cx - 18, cy + 11, 6, ink);
        g16_circfill(cg, cx + 17, cy + 11, 6, ink);
        for (int i = 0; i < 8; i++)             /* the tail */
            g16_rectfill(cg, cx - 14 - i, cy + 17 + i, 9 - i, 1, ink);
        g16_text_scaled(cg, cx - 7, cy - 16, "?", dark, 2);
        break;
    case MENU_ICON_CODE:                        /* a page of code */
        g16_rectfill(cg, cx - 22, cy - 19, 44, 38, ink);
        g16_rectfill(cg, cx - 19, cy - 13, 38, 29, dark);
        for (int i = 0; i < 4; i++) {
            int indent = (i == 1 || i == 2) ? 8 : 0;
            g16_rectfill(cg, cx - 15 + indent, cy - 9 + i * 6, i == 2 ? 12 : 20 - indent, 2,
                         i == 1 ? g16_rgb(255, 122, 176) : i == 2 ? g16_rgb(112, 208, 255)
                                                                 : g16_rgb(200, 205, 220));
        }
        break;
    }
}

int menu_make_tool_cover(g16_sheet_t *s, const char *title, int icon, uint32_t rgb)
{
    if (g16_sheet_alloc(s, BM_COVER_W, BM_COVER_H) != 0)
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

/* More than two rows: the bar says where the two on screen are, so the
 * rows scrolled out at the top are not taken for gone. */
static void scroll_bar(int rows, float first)
{
    int th = SBAR_H * 2 / rows;
    if (th < 24)
        th = 24;
    float k = first / (float)(rows - 2);
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    int ty = GRID_Y0 + (int)lroundf(k * (float)(SBAR_H - th));
    uint16_t track = c16(C_LINE), thumb = c16(C_TEXT);
    if (dim) {
        track = half(track);
        thumb = half(thumb);
    }
    round_rect(SBAR_X, GRID_Y0, SBAR_W, SBAR_H, SBAR_W / 2, track);
    round_rect(SBAR_X, ty, SBAR_W, th, SBAR_W / 2, thumb);
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

static uint32_t over(uint32_t b, uint32_t c, int a);

/* A cover that has not arrived yet (the Market): the title on the plain
 * card, on the text grid, and two bars that breathe under it. */
static void placeholder(const menu_item_t *it, int x, int y, float t, int i)
{
    char lines[2][15] = { "", "" };
    const char *p = it->title ? it->title : "";
    for (int n = 0; n < 2 && *p; n++) {
        while (*p == ' ') p++;
        int len = (int)strlen(p), take = len <= 14 ? len : 14;
        if (len > 14)
            for (int k = 14; k > 0; k--)
                if (p[k] == ' ') { take = k; break; }
        memcpy(lines[n], p, (size_t)take);
        lines[n][take] = 0;
        p += take;
    }
    for (int n = 0; n < 2; n++) {
        int ty = y + 16 + 16 * n;
        if (lines[n][0] && ty >= GRID_TOP && ty + 16 <= GRID_BOT)
            g16_text(&g, x + 8, ty, lines[n], dim ? c16(0x4D4D58) : c16(C_DIM));
    }
    if (it->badge || it->busy || it->running)
        return;                         /* the badge is where the bars go */
    float k = 0.5f + 0.5f * sinf(t * 3.0f - (float)i * 0.6f);
    uint16_t bar = c16(over(0x3A3A4A, 0x5A5A70, (int)(k * 255.0f)));
    if (dim)
        bar = half(bar);
    round_rect(x + 8, y + 58, 72, 6, 3, bar);
    round_rect(x + 8, y + 68, 44, 6, 3, bar);
}

/* A pill on the lower part of a cover ("Playing", "Installed", "42%"), on
 * the text grid; `hot`: on the accent colour. */
static void cover_badge(int x, int y, const char *text, int hot)
{
    int tx = (x + 8 + 7) / 8 * 8, ty = (y + CARD_H - 20) / 16 * 16, n = (int)strlen(text);
    if (ty < GRID_TOP || ty + 16 > GRID_BOT)
        return;
    uint32_t bg = hot ? C_ACCENT : 0x101014, fg = hot ? C_BAR : C_TEXT;
    round_rect(tx - 6, ty - 2, n * 8 + 12, 20, 10, dim ? half(c16(bg)) : c16(bg));
    g16_text(&g, tx, ty, text, dim ? c16(C_DIM) : c16(fg));
}

/* a download: a bar along the bottom of the cover */
static void cover_progress(int x, int y, int percent)
{
    int w = CARD_W - 16, fill = w * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100;
    round_rect(x + 8, y + CARD_H - 6, w, 4, 2, c16(C_LINE));
    if (fill > 0)
        round_rect(x + 8, y + CARD_H - 6, fill, 4, 2, dim ? half(c16(C_ACCENT)) : c16(C_ACCENT));
}

/* text on a pill-shaped background, starting at pixel x */
static void pill_at(int x, int row, const char *s, uint32_t fg, uint32_t bg)
{
    int n = (int)strlen(s);
    round_rect(x - 8, row * 16 - 4, (n + 2) * 8, 24, 12, c16(bg));
    g16_text(&g, x, row * 16, s, c16(fg));
}

/* the same at text column `col` */
static int pill_text(int col, int row, const char *s, uint32_t fg, uint32_t bg)
{
    pill_at(col * 8, row, s, fg, bg);
    return col + (int)strlen(s) + 2;
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

/* a prompt of prompts.c over the bar's colour at (x, y); returns its right edge */
static int put_prompt(const prompt_t *p, int x, int y)
{
    if (!p)
        return x;
    for (int j = 0; j < PROMPT_H; j++) {
        uint16_t *row = g.px + (uint32_t)(y + j) * g.stride;
        for (int i = 0; i < p->w; i++) {
            uint32_t c = p->px[j * p->w + i], a = c >> 24;
            if (a && x + i >= 0 && x + i < SW)
                row[x + i] = c16(over(C_BAR, c & 0xFFFFFF, (int)a));
        }
    }
    return x + p->w;
}

/* the buttons of the hints, for the device pressed last */
enum { BTN_A, BTN_B, BTN_X, BTN_CHANGE, BTN_MONITOR, BTN_Y };

/* up to three prompts for a button, and what goes between them */
static int button_prompts(const menu_view_t *v, int b, const prompt_t *p[3], const char **join)
{
    static const int ds4[3] = { PROMPT_CROSS, PROMPT_CIRCLE, PROMPT_SQUARE };
    static const int pad[3] = { PROMPT_PAD_A, PROMPT_PAD_B, PROMPT_PAD_X };
    *join = "";
    if (v->prompts == MENU_PROMPTS_KEYBOARD) {
        switch (b) {                            /* the keys the menu takes */
        case BTN_A: p[0] = prompt_get(PROMPT_KEY_ENTER, 0); return 1;
        case BTN_B: p[0] = prompt_get(PROMPT_KEY_ESC, 0); return 1;
        case BTN_X: p[0] = prompt_key('C'); return 1;
        case BTN_Y: p[0] = prompt_key('V'); return 1;
        case BTN_CHANGE:
            p[0] = prompt_get(PROMPT_KEY_LEFT, 0);
            p[1] = prompt_get(PROMPT_KEY_RIGHT, 0);
            return 2;
        default:                                /* the monitor: Ctrl+Shift+Esc (the system's keys) */
            p[0] = prompt_get(PROMPT_KEY_CTRL, 0);
            p[1] = prompt_get(PROMPT_KEY_SHIFT, 0);
            p[2] = prompt_get(PROMPT_KEY_ESC, 0);
            *join = "+";
            return 3;
        }
    }
    int ds = v->prompts != MENU_PROMPTS_PAD;
    if (!ds && v->confirm_b && (b == BTN_A || b == BTN_B))
        b = b == BTN_A ? BTN_B : BTN_A;         /* confirm on B, back on A */
    switch (b) {
    case BTN_A: case BTN_B: case BTN_X:
        p[0] = ds ? prompt_get(ds4[b], v->prompts_colour) : prompt_get(pad[b], 0);
        return 1;
    case BTN_Y:
        p[0] = ds ? prompt_get(PROMPT_TRIANGLE, v->prompts_colour) : prompt_get(PROMPT_PAD_Y, 0);
        return 1;
    case BTN_CHANGE:
        p[0] = prompt_get(PROMPT_DPAD_LEFTRIGHT, 0);
        return 1;
    default:                                    /* Start+Select */
        p[0] = prompt_get(ds ? PROMPT_SHARE : PROMPT_PAD_SELECT, 0);
        p[1] = prompt_get(ds ? PROMPT_OPTIONS : PROMPT_PAD_START, 0);
        *join = "+";
        return 2;
    }
}

/* the button's prompts from 4 px left of text column `col`, then a label
 * on the text grid after them; returns the column after the label and a
 * gap */
static int hint(const menu_view_t *v, int col, int row, int b, const char *label)
{
    const prompt_t *p[3] = { NULL, NULL, NULL };
    const char *join;
    int n = button_prompts(v, b, p, &join);
    int x = col * 8 - 4, y = row * 16;
    for (int i = 0; i < n; i++) {
        if (i) {
            x += 2;
            if (join[0]) {
                g16_text(&g, x, y, join, c16(C_TEXT));
                x += 8 * (int)strlen(join) + 2;
            }
        }
        x = put_prompt(p[i], x, y);
    }
    int lc = (x + 4 + 7) / 8;
    g16_text(&g, lc * 8, y, label, c16(C_TEXT));
    /* the pointer (M32) clicks it: A, B, X; "Change" is A on a choice row */
    static const char letter[] = { 'A', 'B', 'X', 'A', 0, 'Y' };
    if (!hints_dead && letter[b])
        zone(col * 8 - 8, y - 4, (lc + (int)strlen(label)) * 8 - (col * 8 - 8) + 4, 24,
             MENU_HIT_BUTTON, letter[b], 1);
    return lc + (int)strlen(label) + 3;
}

/* a small triangle pointing up (dir -1) or down (+1), for the scroll marks */
static void scroll_mark(int cx, int cy, int dir, uint16_t c)
{
    for (int i = 0; i < 5; i++)
        hspan(cx - i, cx + i + 1, cy - dir * (i - 2), c);
}

/* panel geometry: text rows 4 (title), 6..16 every other one (the rows),
 * 18 (help); columns 10-70 on the Pi, 2-43 on the narrow layout */
#define PANEL_X     (WIDE ? 10 * 8 : 2 * 8)
#define PANEL_W     (SW - 2 * PANEL_X)
#define PANEL_Y     52
#define PANEL_H     (310 - PANEL_Y)
#define PANEL_ROW0  6
#define PANEL_COL   (PANEL_X / 8)
#define LABEL_COL   (PANEL_COL + 3)
#define VALUE_END   ((PANEL_X + PANEL_W) / 8 - 3)   /* values end before this column */

/* faded: a question is over it */
static void draw_panel(const menu_panel_t *p, int faded)
{
    round_rect(PANEL_X - 2, PANEL_Y - 2, PANEL_W + 4, PANEL_H + 4, 16, c16(C_LINE));
    round_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, c16(C_BAR));
    zone(PANEL_X - 2, PANEL_Y - 2, PANEL_W + 4, PANEL_H + 4, MENU_HIT_PANEL, 0, 1);
    char buf[72];
    ksnprintf(buf, sizeof buf, "%s", p->title ? p->title : "");
    buf[PANEL_W / 8 - 8] = 0;
    g16_text(&g, (PANEL_COL + 2) * 8, 4 * 16, buf, c16(C_TEXT));
    g16_rectfill(&g, PANEL_X + 16, 88, PANEL_W - 32, 1, c16(C_LINE));
    for (int i = 0; i < MENU_PANEL_ROWS && p->top + i < p->n; i++) {
        const menu_row_t *r = &p->rows[p->top + i];
        int row = PANEL_ROW0 + 2 * i, y = row * 16, sel = p->top + i == p->sel;
        zone(PANEL_X + 16, y - 8, PANEL_W - 32, 32, MENU_HIT_ROW, p->top + i, 1);
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
        buf[PANEL_W / 8 - 4] = 0;
        g16_text(&g, (PANEL_COL + 2) * 8, 18 * 16, buf, c16(C_DIM));
    }
}

/* ---------------------------------------------------------------- the Lib tab */

/* geometry: the groups on text row 4, the list (columns 2-31) and the
 * preview box (columns 35-77) on rows 6-18, the details on rows 14-18 */
#define LIB_GROUP_ROW   4
#define LIB_ROW0        6
#define LIB_LIST_X      (2 * 8)
#define LIB_LIST_W      (30 * 8)
#define LIB_COL_END     32                      /* the list's values end before this column */
#define LIB_BOX_X       (35 * 8)
#define LIB_BOX_W       (42 * 8)
#define LIB_BOX_Y       (LIB_ROW0 * 16 - 4)
#define LIB_BOX_H       (8 * 16)
#define LIB_INFO_ROW    14
#define C_HEAD          0x2A2A36                /* the row of a file */

static void draw_lib(const menu_lib_t *l)
{
    char buf[64];
    /* the groups, on a bar of their own: the selected one on a pill like the tabs */
    int col = 5;
    round_rect(LIB_LIST_X - 8, LIB_GROUP_ROW * 16 - 8, SW - 2 * (LIB_LIST_X - 8), 32, 10, c16(C_BAR));
    g16_text(&g, 2 * 8, LIB_GROUP_ROW * 16, "<", c16(C_DIM));
    for (int i = 0; i < l->ngroups; i++) {
        int n = (int)strlen(l->groups[i]);
        zone(col * 8 - 8, LIB_GROUP_ROW * 16 - 6, (n + 2) * 8, 28, MENU_HIT_GROUP, i, 1);
        if (i == l->group)
            pill_text(col, LIB_GROUP_ROW, l->groups[i], C_BAR, C_TAB_ON);
        else
            g16_text(&g, col * 8, LIB_GROUP_ROW * 16, l->groups[i], c16(C_TEXT));
        col += n + 3;
    }
    g16_text(&g, col * 8 - 8, LIB_GROUP_ROW * 16, ">", c16(C_DIM));

    /* the list */
    round_rect(LIB_LIST_X - 8, LIB_BOX_Y - 4, LIB_LIST_W + 16, MENU_LIB_ROWS * 16 + 16, 10, c16(C_BAR));
    if (!l->n && l->empty) {
        ksnprintf(buf, sizeof buf, "%s", l->empty);
        buf[29] = 0;
        g16_text(&g, LIB_LIST_X + 8, LIB_ROW0 * 16, buf, c16(C_DIM));
    }
    for (int i = 0; i < MENU_LIB_ROWS && l->top + i < l->n; i++) {
        const menu_lib_row_t *r = &l->rows[l->top + i];
        int y = (LIB_ROW0 + i) * 16, sel = l->top + i == l->sel;
        uint16_t fg = c16(C_TEXT), fv = c16(C_DIM);
        if (r->header) {
            g16_rectfill(&g, LIB_LIST_X, y, LIB_LIST_W, 16, c16(C_HEAD));
            fg = c16(C_DIM);
        } else {
            zone(LIB_LIST_X, y, LIB_LIST_W, 16, MENU_HIT_LIB, l->top + i, 1);
            if (sel) {
                g16_rectfill(&g, LIB_LIST_X, y, LIB_LIST_W, 16, c16(C_TAB_ON));
                fg = c16(C_BAR);
                fv = c16(0x4A4A56);
            }
        }
        int indent = r->header ? 1 : 2;
        ksnprintf(buf, sizeof buf, "%s", r->label ? r->label : "");
        buf[22] = 0;
        g16_text(&g, LIB_LIST_X + indent * 8 - 8, y, buf, fg);
        if (r->value && r->value[0]) {
            ksnprintf(buf, sizeof buf, "%s", r->value);
            buf[8] = 0;
            g16_text(&g, (LIB_COL_END - (int)strlen(buf)) * 8, y, buf, fv);
        }
    }
    if (l->top > 0)                             /* in the panel's margin, above and below the rows */
        scroll_mark(LIB_LIST_X + LIB_LIST_W - 4, LIB_ROW0 * 16 - 5, -1, c16(C_DIM));
    if (l->top + MENU_LIB_ROWS < l->n)
        scroll_mark(LIB_LIST_X + LIB_LIST_W - 4, (LIB_ROW0 + MENU_LIB_ROWS) * 16 + 5, 1, c16(C_DIM));

    /* the preview and the details */
    round_rect(LIB_BOX_X - 8, LIB_BOX_Y - 4, LIB_BOX_W + 16, MENU_LIB_ROWS * 16 + 16, 10, c16(C_BAR));
    g16_rectfill(&g, LIB_BOX_X, LIB_BOX_Y, LIB_BOX_W, LIB_BOX_H, c16(C_PILL));
    if (l->preview) {
        g16_clip(&g, LIB_BOX_X, LIB_BOX_Y, LIB_BOX_W, LIB_BOX_H);
        l->preview(&g, LIB_BOX_X, LIB_BOX_Y, LIB_BOX_W, LIB_BOX_H, l->ctx);
        g16_clip(&g, 0, 0, 0, 0);
    }
    for (int i = 0; i < 5; i++) {
        if (!l->lines[i] || !l->lines[i][0])
            continue;
        ksnprintf(buf, sizeof buf, "%s", l->lines[i]);
        buf[42] = 0;
        g16_text(&g, LIB_BOX_X, (LIB_INFO_ROW + i) * 16, buf, i == 0 ? c16(0xFFB040) : c16(i == 1 ? C_TEXT : C_DIM));
    }
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
 * player, the mice (M32: no number, a blue dot on Bluetooth), then WiFi or
 * Ethernet when the console is on a network */
static void status_icons(const menu_view_t *v)
{
    int icon[7], num[7], bt[7], n = 0;
    for (int p = 0; p < 4; p++)
        if (v->dev[p] != MENU_DEV_NONE) {
            icon[n] = v->dev[p] == MENU_DEV_KEYBOARD ? ICON_KEYBOARD : ICON_PAD;
            bt[n] = v->bt >> p & 1;
            num[n++] = p + 1;
        }
    if (v->mice & POINTER_USB) {
        icon[n] = ICON_MOUSE;
        bt[n] = 0;
        num[n++] = 0;
    }
    if (v->mice & POINTER_BLUETOOTH) {
        icon[n] = ICON_MOUSE;
        bt[n] = 1;
        num[n++] = ICON_DOT;
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
            put_icon(m, x, y0, i >= players && v->net_wait ? C_DIM : C_TEXT,
                     bt[i] ? C_BT : C_TEXT, bt[i] ? C_TEXT : C_BAR);
        x += ICON_W + (i + 1 == players ? net_gap : gap);
    }
}

/* ---------------------------------------------------------------- F12: the keys */

/* the menu's own keys on the keyboard (the system's are in syskeys.c) */
static const struct { const char *keys, *what; } menu_keys[] = {
    { "up down left right", "choose (or w a s d)" },
    { "enter", "play, open, yes" },
    { "c", "options of the game" },
    { "v", "the Lib tab: listen" },
    { "q / e", "the tab before, after" },
    { "tab", "the next tab" },
    { "1 - 5", "Market ... Settings" },
    { "r", "read the SD card again" },
    { "mouse", "click: play; right: back" },
};

/* a key's picture by its name ("ctrl", "f12", "s"), as the hints draw them */
static const prompt_t *key_prompt(const char *n)
{
    static const struct { const char *name; int id; } named[] = {
        { "up", PROMPT_KEY_UP }, { "down", PROMPT_KEY_DOWN }, { "left", PROMPT_KEY_LEFT },
        { "right", PROMPT_KEY_RIGHT }, { "enter", PROMPT_KEY_ENTER }, { "esc", PROMPT_KEY_ESC },
        { "space", PROMPT_KEY_SPACE }, { "tab", PROMPT_KEY_TAB }, { "shift", PROMPT_KEY_SHIFT },
        { "ctrl", PROMPT_KEY_CTRL }, { "alt", PROMPT_KEY_ALT }, { "del", PROMPT_KEY_DEL },
    };
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++)
        if (!strcmp(n, named[i].name))
            return prompt_get(named[i].id, 0);
    if (n[0] == 'f' && n[1] >= '1' && n[1] <= '9') {
        int k = atoi(n + 1);
        if (k >= 1 && k <= 12)
            return prompt_get(PROMPT_KEY_F1 + k - 1, 0);
    }
    if (n[0] && !n[1])
        return prompt_key(n[0] >= 'a' && n[0] <= 'z' ? n[0] - 32 : n[0]);
    return NULL;
}

/* the keys' pictures from x on text row `row`; the x after them */
static int key_pictures(const char *keys, int x, int row)
{
    for (const char *s = keys; *s;) {
        while (*s == ' ')
            s++;
        char tok[16];
        size_t n = 0;
        while (s[n] && s[n] != ' ') {
            if (n + 1 < sizeof tok)
                tok[n] = s[n];
            n++;
        }
        tok[n < sizeof tok ? n : sizeof tok - 1] = 0;
        s += n;
        if (!tok[0])
            break;
        const prompt_t *p = strcmp(tok, "/") && strcmp(tok, "-") ? key_prompt(tok) : NULL;
        if (p) {
            x = put_prompt(p, x, row * 16) + 2;
        } else {
            g16_text(&g, x + 2, row * 16, tok, c16(C_DIM));
            x += 8 * (int)strlen(tok) + 4;
        }
    }
    return x;
}

/* While F12 is held: the system's keys, then the menu's, with their
 * pictures, over everything (columns on the text grid) */
static void keys_help(void)
{
    g16_rectfill(&g, 0, 0, SW, SH, c16(C_BAR));
    g16_text(&g, 2 * 8, 0, "Keys", c16(C_ACCENT));
    g16_text(&g, 7 * 8, 0, "(F12 held)", c16(C_DIM));
    /* the system's keys in the first column, the menu's in the next (one
     * column: one after the other) */
    const int cols = WIDE ? 2 : 1, colw = (SW / 8 - 2) / cols, rows = SH / 16 - 2;
    const int nsys = syskeys_count(), nmenu = (int)(sizeof menu_keys / sizeof menu_keys[0]);
    for (int i = 0; i < 2 + nsys + nmenu; i++) {
        const int at = cols > 1 && i > nsys ? rows + (i - nsys - 1) : i;
        const int c = at / rows, r = at % rows;
        if (c >= cols)
            break;
        const int col = 2 + c * colw, row = 2 + r;
        const char *keys, *what;
        if (i == 0 || i == nsys + 1) {
            g16_text(&g, col * 8, row * 16, i == 0 ? "bm" : "Menu", c16(0xFFB040));
            continue;
        }
        if (i <= nsys) {
            keys = syskey(i - 1)->keys;
            what = syskey(i - 1)->what;
        } else {
            keys = menu_keys[i - nsys - 2].keys;
            what = menu_keys[i - nsys - 2].what;
        }
        int x = key_pictures(keys, col * 8, row);
        int tc = (x + 8 + 7) / 8;               /* the text on the grid (the tests read it) */
        char buf[48];
        ksnprintf(buf, sizeof buf, "%s", what);
        int room = col + colw - 1 - tc;
        if (room < (int)strlen(buf))
            buf[room > 0 ? room : 0] = 0;
        g16_text(&g, tc * 8, row * 16, buf, c16(C_TEXT));
    }
}

/* ---------------------------------------------------------------- screen */

int menu_ui_cols(void)
{
    return cols;
}

/* the screen of the layout: on the RGB30 360x360 shown 2x (the display
 * controller enlarges it); on the Pi 640x360, or 1920x1080 with
 * menu_scale=3 in bm/config.txt (the ARM enlarges it at each flip: a try,
 * the GPU will draw it there one day) */
static int screen_open(framebuffer_t *fb)
{
#ifdef BM_RGB30
    sw = 360;
    scale = 1;
    return fb_init_mode(fb, (uint32_t)sw, (uint32_t)sh, 3, 16, 2, 0);
#else
    const char *ms = config_get("menu_scale");
    sw = 640;
    scale = ms && strcmp(ms, "3") == 0 ? 3 : 1;
    if (scale > 1 && !frame)
        frame = malloc(MAX_W * MAX_H * 2);
    if (scale > 1 && (!frame || fb_init_depth(fb, (uint32_t)(sw * scale), (uint32_t)(sh * scale), 3, 16) != 0))
        scale = 1;
    if (scale > 1)
        return 0;
    return fb_init_depth(fb, (uint32_t)sw, (uint32_t)sh, 3, 16);
#endif
}

int menu_ui_open(framebuffer_t *fb)
{
    if (!bg_cur) {
        bg_cur = malloc(MAX_W * MAX_H * 2);
        bg_prev = malloc(MAX_W * MAX_H * 2);
        if (!bg_cur || !bg_prev) {
            free(bg_cur); free(bg_prev);
            bg_cur = bg_prev = NULL;
            return -1;
        }
    }
    con_w = fb->width;
    con_h = fb->height;
    console_suspend(1);
    if (screen_open(fb) != 0) {
        fb_init(fb, con_w, con_h, 2);
        console_suspend(0);
        return -1;
    }
    cols = (SW - 16) / (CARD_W + GAP_X);    /* 4 at 640, 2 at 360 */
    ready = 1;
    bg_valid = 0;
    fade = 0;
    nzones = 0;
    pointer_env(1, SW, SH);
    t0 = timer_ticks();
    deadline = t0 + FRAME_US;
    return 0;
}

void menu_ui_close(framebuffer_t *fb)
{
    if (!ready)
        return;
    ready = 0;
    nzones = 0;
    pointer_env(0, 0, 0);
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

/* the layout's frame `scale` times bigger on the page (each row widened
 * once, then copied down) */
static void enlarge(framebuffer_t *fb)
{
    uint32_t stride = fb->pitch / 2;
    for (int y = 0; y < SH; y++) {
        uint16_t *d = (uint16_t *)fb->base + (uint32_t)(y * scale) * stride;
        const uint16_t *s = frame + y * SW;
        for (int x = 0; x < SW; x++)
            for (int k = 0; k < scale; k++)
                d[x * scale + k] = s[x];
        for (int k = 1; k < scale; k++)
            memcpy(d + (uint32_t)k * stride, d, (size_t)(SW * scale) * 2);
    }
}

void menu_ui_frame(framebuffer_t *fb, const menu_view_t *v)
{
    if (!ready)
        return;
    if (scale > 1)
        g16_target(&g, frame, (uint32_t)SW, SW, SH, &font_console_8x16);
    else
        g16_target(&g, (uint16_t *)fb->base, fb->pitch / 2, SW, SH, &font_console_8x16);
    float t = (float)(timer_ticks() - t0) * 1e-6f;
    const menu_item_t *cur = v->sel >= 0 && v->sel < v->n ? &v->items[v->sel] : NULL;
    dim = v->panel != NULL;
    nzones = 0;

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
    int sel_row = v->sel / cols;
    if (sel_row < first_row) first_row = sel_row;
    if (sel_row > first_row + 1) first_row = sel_row - 1;
    if (first_row < 0) first_row = 0;
    scroll += ((float)first_row - scroll) * 0.25f;
    if (fabsf(scroll - (float)first_row) < 0.01f) scroll = (float)first_row;

    g16_clip(&g, 0, GRID_TOP, SW, GRID_BOT - GRID_TOP);
    for (int i = 0; !v->lib && i < v->n; i++) {
        int row = i / cols, col = i % cols;
        int x = GRID_X0 + col * (CARD_W + GAP_X);
        int y = GRID_Y0 + (int)lroundf(((float)row - scroll) * PITCH_Y);
        if (y + CARD_H + 8 < GRID_TOP || y - 8 >= GRID_BOT)
            continue;
        {
            int z0 = y < GRID_TOP ? GRID_TOP : y, z1 = y + CARD_H > GRID_BOT ? GRID_BOT : y + CARD_H;
            zone(x, z0, CARD_W, z1 - z0, MENU_HIT_COVER, i, y >= GRID_TOP && y + CARD_H <= GRID_BOT);
        }
        if (i == v->sel)            /* ring with a gap, like the home screens */
            round_ring(x - 6, y - 6, CARD_W + 12, CARD_H + 12, RADIUS + 6, 3,
                       dim ? c16(0x45454B) : pulse(t));
        const menu_item_t *it = &v->items[i];
        card(it->cover, x, y);
        if (it->loading && !it->cover)
            placeholder(it, x, y, t, i);
        if (it->busy) {
            char pc[8];
            ksnprintf(pc, sizeof pc, "%d%%", it->percent);
            cover_badge(x, y, pc, 1);
            cover_progress(x, y, it->percent);
        } else if (it->running) {
            cover_badge(x, y, "Playing", 0);
        } else if (it->badge) {
            cover_badge(x, y, it->badge, strcmp(it->badge, "Update") == 0);
        }
    }
    g16_clip(&g, 0, 0, 0, 0);
    if (!v->lib && v->n > 2 * cols)
        scroll_bar((v->n + cols - 1) / cols, scroll);
    if (v->lib && !v->on_gear)
        draw_lib(v->lib);

    /* top bar: the tabs and Settings (the last tab: its panel; L1 / R1 move
     * between them), then the players and the network */
    g16_rectfill(&g, 0, 0, SW, BAR_H, c16(C_BAR));
    /* the first tab (the Market) off the screen at the left, the end of its
     * name showing, unless it is the tab: then all of them slide right */
    {
        float to = 0;
        if (v->peek_first && v->ntabs > 1 && (v->tab != 0 || v->on_gear))
            to = -8.0f * (float)(strlen(v->tabs[0]) + 1);
        tab_shift += (to - tab_shift) * 0.3f;
        if (fabsf(tab_shift - to) < 0.5f)
            tab_shift = to;                     /* at rest on the font's columns */
    }
    const int ox = (int)lroundf(tab_shift);
    int col = 3;
    for (int i = 0; i < v->ntabs; i++) {
        int n = (int)strlen(v->tabs[i]), x = col * 8 + ox;
        int zx = x - 8 < 0 ? 0 : x - 8, zw = x - 8 + (n + 2) * 8 - zx;
        zone(zx, 4, zw, 40, MENU_HIT_TAB, i, 1);
        if (i == v->tab && !v->on_gear)
            pill_at(x, 1, v->tabs[i], C_BAR, C_TAB_ON);
        else
            g16_text(&g, x, 16, v->tabs[i], c16(C_DIM));
        col += n + 4;
    }
    zone(col * 8 + ox - 8, 4, 10 * 8, 40, MENU_HIT_SETTINGS, 0, 1);
    if (v->on_gear)
        pill_at(col * 8 + ox, 1, "Settings", C_BAR, C_TAB_ON);
    else
        g16_text(&g, col * 8 + ox, 16, "Settings", c16(C_DIM));
    status_icons(v);

    /* the name of the selected cartridge, on a pill */
    if (v->panel || v->lib) {
        /* the panel covers it; the Lib tab has its groups there */
    } else if (v->banner) {
        char buf[72];
        ksnprintf(buf, sizeof buf, "%s", v->banner);
        if (SW / 8 - 6 < (int)sizeof buf)       /* no longer than the layout */
            buf[SW / 8 - 6] = 0;
        pill_text(3, TITLE_ROW, buf, C_DIM, C_PILL);
    } else if (cur && cur->title && cur->title[0]) {
        char buf[72];
        ksnprintf(buf, sizeof buf, "%s", cur->title);
        if (SW / 8 - 6 < (int)sizeof buf)
            buf[SW / 8 - 6] = 0;
        int n = (int)strlen(buf);
        round_rect(3 * 8 - 10, TITLE_ROW * 16 - 6, (n + 2) * 8 + 4, 28, 14, c16(C_LINE));
        pill_text(3, TITLE_ROW, buf, C_TEXT, C_PILL);
    } else if (v->n == 0) {
        pill_text(3, TITLE_ROW, "nothing here yet", C_DIM, C_PILL);
    }

    /* bottom bar: details, last game, buttons */
    hints_dead = v->ask != NULL;
    g16_rectfill(&g, 0, FOOT_Y, SW, SH - FOOT_Y, c16(C_BAR));
    g16_rectfill(&g, 16, FOOT_Y + 1, SW - 32, 1, c16(C_LINE));
    char buf[96];
    int shown = 0;
    if (v->details && !v->panel) {
        ksnprintf(buf, sizeof buf, "%s", v->details);
        buf[SW / 8 - 4] = 0;
        g16_text(&g, 2 * 8, 20 * 16, buf, c16(C_DIM));
        shown = 1;
    }
    /* the note left of the hints; on the narrow layout over them, if free */
    if (v->note && v->note[0] && (WIDE || !shown)) {
        ksnprintf(buf, sizeof buf, "%s", v->note);
        buf[WIDE ? 30 : SW / 8 - 4] = 0;
        g16_text(&g, 2 * 8, (WIDE ? 21 : 20) * 16, buf, c16(C_DIM));
    }
    const int hc = WIDE ? 34 : 2;               /* where the hints start */
    if (v->panel) {
        const menu_row_t *r = v->panel->sel < v->panel->n ? &v->panel->rows[v->panel->sel] : NULL;
        col = WIDE ? 40 : hc;
        if (r && r->kind == MENU_ROW_CHOICE)
            col = hint(v, col, 21, BTN_CHANGE, "Change");
        else if (r && r->kind != MENU_ROW_INFO)
            col = hint(v, col, 21, BTN_A, r->kind == MENU_ROW_SUB ? "Open" : "Select");
        hint(v, WIDE && col < 55 ? 55 : col, 21, BTN_B, "Back");
    } else if (v->on_gear) {
        col = hint(v, hc, 21, BTN_A, "Settings");
        if (!v->no_monitor)
            hint(v, col, 21, BTN_MONITOR, "Monitor");
    } else if (v->lib) {
        col = hc;
        if (v->lib->open)
            col = hint(v, col, 21, BTN_A, v->lib->open);
        if (v->lib->play)
            col = hint(v, col, 21, BTN_Y, v->lib->play);
        if (!v->no_monitor)
            hint(v, col, 21, BTN_MONITOR, "Monitor");
    } else {
        const char *a = v->a_label ? v->a_label
                      : cur && cur->kind && strcmp(cur->kind, "tool") == 0 ? "Open" : "Play";
        col = hc;
        if (a[0])
            col = hint(v, col, 21, BTN_A, a);
        if (cur && cur->kind && strcmp(cur->kind, "market") == 0 && cur->title && cur->title[0])
            col = hint(v, col, 21, BTN_X, "Details");
        else if (v->n && v->items[v->sel].path && v->items[v->sel].path[0])
            col = hint(v, col, 21, BTN_X, "Options");
        if (!v->no_monitor)
            hint(v, col, 21, BTN_MONITOR, "Monitor");
    }

    hints_dead = 0;
    if (v->panel)
        draw_panel(v->panel, v->ask != NULL);

    /* a question in a panel over everything */
    if (v->ask) {
        const int px = WIDE ? 12 * 8 : 2 * 8, py = 8 * 16 - 8, pw = SW - 2 * px, ph = 5 * 16 + 16;
        round_rect(px - 2, py - 2, pw + 4, ph + 4, 14, c16(C_LINE));
        round_rect(px, py, pw, ph, 12, c16(C_BAR));
        zone(px - 2, py - 2, pw + 4, ph + 4, MENU_HIT_ASK, 0, 1);
        char q[64];
        ksnprintf(q, sizeof q, "%s", v->ask);
        q[pw / 8 - 2] = 0;
        g16_text(&g, (SW / 8 - (int)strlen(q)) / 2 * 8, 9 * 16, q, c16(C_TEXT));
        if (v->ask_detail) {
            ksnprintf(q, sizeof q, "%s", v->ask_detail);
            q[pw / 8 - 2] = 0;
            g16_text(&g, (SW / 8 - (int)strlen(q)) / 2 * 8, 10 * 16, q, c16(C_DIM));
        }
        int c = hint(v, WIDE ? 26 : 4, 12, BTN_A, v->ask_yes ? v->ask_yes : "Close it");
        hint(v, c + 2, 12, BTN_B, "Cancel");
    }

    if (v->keys_help)
        keys_help();
    pointer_draw(g.px, g.stride, SW, SH);       /* the arrow over everything */
    if (scale > 1)
        enlarge(fb);
    fb_flip(fb);
    /* what is left of the frame: the Market's work, if any (a little
     * margin for the flip), then the wait */
    if (v->idle && (int32_t)(deadline - 1500 - timer_ticks()) > 0)
        v->idle(deadline - 1500);
    while ((int32_t)(timer_ticks() - deadline) < 0)
        ;
    uint32_t now = timer_ticks();
    deadline += FRAME_US;
    if ((int32_t)(now - deadline) > 0)
        deadline = now + FRAME_US;
}
