#include "menu_ui.h"
#include "config.h"
#include "fiber.h"
#include "icons.h"
#include "menu_bg.h"
#include "pointer.h"
#include "prompts.h"
#include "stalls.h"
#include "syskeys.h"
#include "bm/bm.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "gpu/gpu3d.h"
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
/* the screen is the layout `scale` times bigger (2 on the Pi by default: 1280x720;
 * menu_scale=3: 1920x1080), drawn in `frame` and enlarged at the flip */
static int scale = 1;
static uint16_t *frame;
static int gpu_big;                     /* bm3d 4.8: the GPU enlarges it (else the ARM) */
static int gpu_draw;                    /* the GPU draws the 1080p layout directly (menu_gpu=1) */
static g16_sheet_t bg_sheet;
static int bg_sheet_alloced;

static inline const font_t *menu_font(int sc)
{
    return sc == 3 ? &font_console_24x48 : sc == 2 ? &font_console_16x32 : &font_console_8x16;
}
#define FRAME_US 16667
#define IDLE_MIN_US 3000                /* the least free time a late frame gives the jobs (Pi), times (menu_scale - 1) */

/* layout: text sits on the 8x16 grid, on solid colours (the QEMU tests
 * read the screen back) */
#define BAR_H       48              /* top bar: rows 0-2 */
#define TITLE_ROW   4               /* name of the selected cartridge */
#define CARD_W      MENU_CARD       /* squares, as the bm Suite's icons */
#define CARD_H      MENU_CARD
#define RADIUS      0               /* square corners of the covers */
#define GAP_X       6
#define PITCH_Y     (CARD_H + GAP_X)
#define GRID_X0     ((SW - (cols * CARD_W + (cols - 1) * GAP_X)) / 2)
#define FOOT_Y      316             /* bottom bar, text rows 20-21 */
#define GRID_BOT    FOOT_Y          /* grid clip */
/* two whole rows, and the top of the next one showing: there is more below */
#define PEEK        16
#define GRID_Y0     (GRID_BOT - PEEK - 2 * PITCH_Y)     /* first row of covers */
#define GRID_TOP    (GRID_Y0 - 8)   /* the selection ring is 6 px out (3 gap + 3 line) */
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
static uint32_t frame_end;          /* when the last frame was over: the drawing's start */
/* the free time of the frames for the work in the background, counted over
 * IDLE_FRAMES (the end of menu_ui_frame): frames that had some, us offered,
 * us taken, the most a frame's work went past its end, the least slack */
#define IDLE_FRAMES 300
#define SLACK_NONE 0x7FFFFFFF
static int idle_frames, idle_gave;
static uint32_t idle_offered, idle_used, idle_over;
static int32_t idle_slack = SLACK_NONE;
static uint32_t st_draw, st_enlarge, st_flip;      /* us over IDLE_FRAMES: drawing, enlarging, flip */
static int idle_first;              /* the frame after the menu opens: its slack is the opening's */
static uint32_t idle_sd_retries, idle_sd_timeouts;  /* sd_counters at the last line */
static float scroll;                /* first visible row, eased */
static float tab_shift;             /* the tabs' x, eased: < 0 while the first one hides */
static int first_row;
static uint16_t *bg_cur, *bg_prev;  /* blurred covers, SW x SH */
static const g16_sheet_t *bg_key;   /* the cover bg_cur was made from */
static int bg_valid, fade;
static menu_bg_t bgb;               /* the picture being made into bg_cur */
static int bg_building;             /* bg_cur is not whole yet: bg_prev is shown */
#define BG_ROWS 24                  /* rows made per frame (tests/ui/test_menu_bg.c: keep equal) */
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

static inline void m_clip(int x, int y, int w, int h)
{
    if (gpu_draw) {
        if (w <= 0 || h <= 0)
            g16_clip(&g, 0, 0, 0, 0);
        else
            g16_clip(&g, x * scale, y * scale, w * scale, h * scale);
    } else {
        g16_clip(&g, x, y, w, h);
    }
}

static inline void m_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0)
        return;
    if (gpu_draw)
        gpu3d_rect2d(&g, x * scale, y * scale, (x + w) * scale, (y + h) * scale, c);
    else
        g16_rectfill(&g, x, y, w, h, c);
}

static inline void m_rect_px(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (x1 <= x0 || y1 <= y0)
        return;
    if (gpu_draw)
        gpu3d_rect2d(&g, x0, y0, x1, y1, c);
    else
        g16_rectfill(&g, x0, y0, x1 - x0, y1 - y0, c);
}

static inline void m_text(int x, int y, const char *str, uint16_t c)
{
    if (gpu_draw) {
        int z = (g.font == menu_font(scale)) ? 1 : scale;
        gpu3d_text2d(&g, x * scale, y * scale, str, c, z);
    } else {
        g16_text(&g, x, y, str, c);
    }
}

static inline void m_text_small(int x, int y, const char *str, uint16_t c)
{
    if (gpu_draw) {
        const font_t *saved = g.font;
        g.font = &font_console_8x16;
        gpu3d_text2d(&g, x * scale, y * scale, str, c, 1);
        g.font = saved;
    } else {
        g16_text(&g, x, y, str, c);
    }
}

static inline void m_pointer(framebuffer_t *fb)
{
    if (gpu_draw)
        pointer_draw((uint16_t *)fb->base, fb->pitch / 2, SW * scale, SH * scale);
    else
        pointer_draw(g.px, g.stride, SW, SH);
}

/* ---------------------------------------------------------------- covers */

/* A decode of a cover runs in a fiber job (covers, Market): it gives the CPU back every COVER_BAND rows
 * (fiber_slice: cheap before the slice ends, nothing outside a fiber) and stops when the job is cancelled.
 * 1: stop, the result is not wanted. The two buffers are the call's own: another job runs in the gaps. */
#define COVER_BAND  4

static int cover_band(int y)
{
    if ((y & (COVER_BAND - 1)) != COVER_BAND - 1)
        return 0;
    fiber_slice();
    return fiber_cancelled();
}

/* the box (cx, cy, cw, ch) of rgba (w wide) into out (CARD_W wide, RGB)
 * at (dx, dy) dw x dh: each pixel the average of what falls on it; 1 if cancelled */
static int area_scale(const uint8_t *rgba, int w, int cx, int cy, int cw, int ch,
                      uint8_t *out, int dx, int dy, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int sy0 = cy + y * ch / dh, sy1 = cy + (y + 1) * ch / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = cx + x * cw / dw, sx1 = cx + (x + 1) * cw / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t acc[3] = { 0, 0, 0 }, n = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
                    n++;
                }
            uint8_t *o = out + ((size_t)(dy + y) * CARD_W + dx + x) * 3;
            o[0] = (uint8_t)(acc[0] / n); o[1] = (uint8_t)(acc[1] / n); o[2] = (uint8_t)(acc[2] / n);
        }
        if (cover_band(y))
            return 1;
    }
    return 0;
}

int menu_load_cover(g16_sheet_t *s, const uint8_t *rgba, int w, int h)
{
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
        return -1;
    enum { LO = 8 };
    uint8_t *out = malloc((size_t)CARD_W * CARD_H * 3);
    uint8_t *lo = malloc((size_t)LO * CARD_W * 3);
    int r = -1;
    if (!out || !lo)
        goto done;
    if (w * 10 >= h * 9 && h * 10 >= w * 9) {
        if (area_scale(rgba, w, 0, 0, w, h, out, 0, 0, CARD_W, CARD_H))     /* (nearly) a square */
            goto done;
    } else {
        /* the middle square in 8x8 blocks, stretched smooth and darker:
         * the rest of the card; the picture whole over it */
        const int c = w < h ? w : h;
        if (area_scale(rgba, w, (w - c) / 2, (h - c) / 2, c, c, lo, 0, 0, LO, LO))
            goto done;
        for (int y = 0; y < CARD_H; y++) {
            float v = (y + 0.5f) * LO / CARD_H - 0.5f;
            int j0 = v < 0 ? 0 : (int)v, j1 = j0 + 1 < LO ? j0 + 1 : LO - 1;
            float fy = v < 0 ? 0 : v - j0;
            for (int x = 0; x < CARD_W; x++) {
                float u = (x + 0.5f) * LO / CARD_W - 0.5f;
                int i0 = u < 0 ? 0 : (int)u, i1 = i0 + 1 < LO ? i0 + 1 : LO - 1;
                float fx = u < 0 ? 0 : u - i0;
                for (int k = 0; k < 3; k++) {
                    float a = lo[(j0 * CARD_W + i0) * 3 + k], b = lo[(j0 * CARD_W + i1) * 3 + k];
                    float cc = lo[(j1 * CARD_W + i0) * 3 + k], d = lo[(j1 * CARD_W + i1) * 3 + k];
                    float top = a + (b - a) * fx, bot = cc + (d - cc) * fx;
                    out[(y * CARD_W + x) * 3 + k] = (uint8_t)((top + (bot - top) * fy) * 0.45f);
                }
            }
            if (cover_band(y))
                goto done;
        }
        const int fw = w >= h ? CARD_W : CARD_W * w / h, fh = w >= h ? CARD_H * h / w : CARD_H;
        if (area_scale(rgba, w, 0, 0, w, h, out, (CARD_W - fw) / 2, (CARD_H - fh) / 2, fw, fh))
            goto done;
    }
    if (g16_sheet_alloc(s, CARD_W, CARD_H) != 0)
        goto done;
    for (int y = 0; y < CARD_H; y++) {
        for (int x = 0; x < CARD_W; x++) {
            const uint8_t *p = out + (y * CARD_W + x) * 3;
            g16_sheet_set(s, x, y, g16_rgb(p[0], p[1], p[2]), 1);
        }
        if (cover_band(y)) {
            g16_sheet_free(s);
            goto done;
        }
    }
    r = 0;
done:
    free(out);
    free(lo);
    return r;
}


int menu_make_cover(g16_sheet_t *s, const char *title)
{
    if (g16_sheet_alloc(s, CARD_W, CARD_H) != 0)
        return -1;
    uint32_t h = 2166136261u;
    for (const char *p = title; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    g16_t cg;
    g16_target(&cg, s->px, (uint32_t)s->w, s->w, s->h, &font_console_8x16);
    uint32_t r = 40 + (h & 63), gg = 40 + (h >> 6 & 63), b = 70 + (h >> 12 & 95);
    for (int y = 0; y < s->h; y++) {
        uint32_t k = 100 + (uint32_t)y * 160 / (uint32_t)s->h;
        g16_rectfill(&cg, 0, y, s->w, 1, g16_rgb(r * k / 160, gg * k / 160, b * k / 160));
    }
    /* the title on up to four lines of 10 characters, split at spaces */
    enum { LINE = 10 };
    char lines[4][LINE + 1] = { "", "", "", "" };
    int n = 0;
    const char *p = title;
    while (*p && n < 4) {
        while (*p == ' ') p++;
        int len = (int)strlen(p);
        int take = len <= LINE ? len : LINE;
        if (len > LINE)
            for (int i = LINE; i > 0; i--)
                if (p[i] == ' ') { take = i; break; }
        memcpy(lines[n], p, (size_t)take);
        lines[n][take] = 0;
        n++;
        p += take;
    }
    int y0 = (s->h - n * 16) / 2 - 6;
    for (int i = 0; i < n; i++) {
        int x = (s->w - (int)strlen(lines[i]) * 8) / 2;
        g16_text(&cg, x + 1, y0 + i * 16 + 1, lines[i], 0);
        g16_text(&cg, x, y0 + i * 16, lines[i], g16_rgb(255, 230, 120));
    }
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

/* As the bm Suite's icons: a diagonal gradient of the colour (lighter at
 * the top left), a soft lighter band across it, the icon in white, the
 * name under it in the small font */
int menu_make_tool_cover(g16_sheet_t *s, const char *title, int icon, uint32_t rgb)
{
    if (g16_sheet_alloc(s, CARD_W, CARD_H) != 0)
        return -1;
    g16_t cg;
    g16_target(&cg, s->px, (uint32_t)s->w, s->w, s->h, &font_console_8x16);
    const int r = (int)(rgb >> 16), gg = (int)(rgb >> 8 & 255), b = (int)(rgb & 255);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            int d = x + y, k = 300 - d * 200 / (s->w + s->h);      /* 300 .. 100 (/200) */
            int band = d > s->h - 10 && d < s->h + 26 ? 26 : 0;   /* the lighter band */
            int c[3] = { r * k / 200 + band, gg * k / 200 + band, b * k / 200 + band };
            for (int i = 0; i < 3; i++)
                c[i] = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
            s->px[y * s->w + x] = g16_rgb((uint32_t)c[0], (uint32_t)c[1], (uint32_t)c[2]);
        }
    draw_icon(&cg, icon, s->w / 2, 34, g16_rgb(245, 245, 250), g16_rgb((uint32_t)r / 3, (uint32_t)gg / 3,
                                                                       (uint32_t)b / 3));
    cg.font = &font_console_6x12;
    char name[16];
    ksnprintf(name, sizeof name, "%s", title);
    if ((int)strlen(name) * 6 > s->w - 6)
        name[(s->w - 6) / 6] = 0;
    int x = (s->w - (int)strlen(name) * 6) / 2;
    g16_text(&cg, x + 1, s->h - 19, name, 0);
    g16_text(&cg, x, s->h - 20, name, g16_rgb(255, 255, 255));
    memset(s->alpha, 1, (size_t)s->w * s->h);
    return 0;
}

/* ---------------------------------------------------------------- background */


/* the background into the frame, cross-fading from the previous one */
static void put_background(void)
{
    if (gpu_draw) {
        if (!bg_sheet_alloced) {
            if (g16_sheet_alloc(&bg_sheet, SW, SH) == 0) {
                memset(bg_sheet.alpha, 1, (size_t)bg_sheet.w * bg_sheet.h);
                bg_sheet_alloced = 1;
            }
        }
        if (fade > 0 && !bg_building) {
            uint32_t a = (uint32_t)(FADE_FRAMES - fade) * 32 / FADE_FRAMES;     /* 0..32 of the new */
            for (int y = 0; y < SH; y++) {
                const uint16_t *p = bg_prev + y * SW, *q = bg_cur + y * SW;
                uint16_t *o = bg_sheet.px + (uint32_t)y * bg_sheet.w;
                for (int x = 0; x < SW; x++) {
                    uint32_t A = (p[x] | (uint32_t)p[x] << 16) & 0x07E0F81Fu;
                    uint32_t B = (q[x] | (uint32_t)q[x] << 16) & 0x07E0F81Fu;
                    uint32_t m = (A * (32 - a) + B * a) >> 5 & 0x07E0F81Fu;
                    o[x] = dim ? half((uint16_t)(m | m >> 16)) : (uint16_t)(m | m >> 16);
                }
            }
            fade--;
            bg_sheet.version++;
        } else {
            for (int y = 0; y < SH; y++) {
                uint16_t *o = bg_sheet.px + (uint32_t)y * bg_sheet.w;
                const uint16_t *q = (bg_building ? bg_prev : bg_cur) + y * SW;
                if (!dim || y < BAR_H || y >= FOOT_Y)
                    memcpy(o, q, SW * 2);
                else
                    for (int x = 0; x < SW; x++)
                        o[x] = half(q[x]);
            }
            bg_sheet.version++;
        }
        if (bg_sheet_alloced)
            gpu3d_blit2d(&g, &bg_sheet, 0, 0, SW, SH, 0, 0, scale, 0, 0);
        return;
    }
    if (fade > 0 && !bg_building) {
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
        const uint16_t *q = (bg_building ? bg_prev : bg_cur) + y * SW;
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
    if (gpu_draw) {
        if (x1 > x0)
            gpu3d_rect2d(&g, x0 * scale, y * scale, x1 * scale, (y + 1) * scale, c);
        return;
    }
    if (y < g.cy0 || y >= g.cy1) return;
    if (x0 < g.cx0) x0 = g.cx0;
    if (x1 > g.cx1) x1 = g.cx1;
    uint16_t *p = g.px + (uint32_t)y * g.stride;
    for (int x = x0; x < x1; x++)
        p[x] = c;
}

static void round_rect(int x, int y, int w, int h, int r, uint16_t c)
{
    if (r <= 0) {
        m_rect(x, y, w, h, c);
        return;
    }
    if (gpu_draw && h > 2 * r) {
        for (int j = 0; j < r; j++) {
            int in = corner_inset(r, j);
            hspan(x + in, x + w - in, y + j, c);
        }
        m_rect(x, y + r, w, h - 2 * r, c);
        for (int j = h - r; j < h; j++) {
            int in = corner_inset(r, h - 1 - j);
            hspan(x + in, x + w - in, y + j, c);
        }
        return;
    }
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
    if (r <= 0) {
        m_rect(x, y, w, t, c);
        m_rect(x, y + h - t, w, t, c);
        m_rect(x, y + t, t, h - 2 * t, c);
        m_rect(x + w - t, y + t, t, h - 2 * t, c);
        return;
    }
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

/* A cover made for the other device: the drawing stays, the colour changes (mostly grey with a cool tint)
 * and a vignette darkens it towards the corners. v: 256 = no darkening, 0 = black. */
static uint16_t cover_grey(uint16_t p, int v)
{
    int r = p >> 11 & 31, g = (p >> 6 & 31), b = p & 31;       /* (g: its top 5 of 6 bits) */
    int l = (r * 77 + g * 150 + b * 29) >> 8;
    r = (l * 160 + r * 96) >> 8;
    g = (l * 160 + g * 96) >> 8;
    b = (l * 160 + b * 96) >> 8;
    r = r > 0 ? r - 1 : 0;                              /* the cool tint */
    b = b < 30 ? b + 2 : 31;
    r = r * v >> 8;
    g = g * v >> 8;
    b = b * v >> 8;
    return (uint16_t)(r << 11 | g << 6 | b);
}

/* the cover with rounded corners, clipped to the grid rows */
static void card(const g16_sheet_t *s, int x, int y, int grey)
{
    const int fade = dim || grey;
    if (gpu_draw) {
        if (s && s->px && s->w >= CARD_W && s->h >= CARD_H && !fade) {
            gpu3d_blit2d(&g, s, 0, 0, CARD_W, CARD_H, x * scale, y * scale, scale, 0, 0);
        } else if (s && s->px && s->w >= CARD_W && s->h >= CARD_H) {
            gpu3d_blit2d(&g, s, 0, 0, CARD_W, CARD_H, x * scale, y * scale, scale, 0, 0);
        } else {
            m_rect(x, y, CARD_W, CARD_H, fade ? half(c16(0x303040)) : c16(0x303040));
        }
        return;
    }
    const int hw = CARD_W / 2, hh = CARD_H / 2, far2 = hw * hw + hh * hh;
    for (int j = 0; j < CARD_H; j++) {
        int yy = y + j;
        if (yy < g.cy0 || yy >= g.cy1)
            continue;
        int dy = j < CARD_H / 2 ? j : CARD_H - 1 - j;
        int in = corner_inset(RADIUS, dy);
        uint16_t *dst = g.px + (uint32_t)yy * g.stride + x + in;
        if (s && s->px && s->w >= CARD_W && s->h >= CARD_H && !fade) {
            memcpy(dst, s->px + j * s->w + in, (size_t)(CARD_W - 2 * in) * 2);
        } else if (s && s->px && s->w >= CARD_W && s->h >= CARD_H) {
            const uint16_t *src = s->px + j * s->w + in;
            for (int i = 0; i < CARD_W - 2 * in; i++) {
                if (grey) {
                    int ddx = i + in - hw, ddy = j - hh;
                    int v = 240 - 95 * (ddx * ddx + ddy * ddy) / far2;      /* 240 in the middle, 145 at the corners */
                    uint16_t c = cover_grey(src[i], v);
                    dst[i] = dim ? half(c) : c;
                } else {
                    dst[i] = half(src[i]);
                }
            }
        } else {
            for (int i = 0; i < CARD_W - 2 * in; i++)
                dst[i] = fade ? half(c16(0x303040)) : c16(0x303040);
        }
    }
}

static uint32_t over(uint32_t b, uint32_t c, int a);

/* A cover that has not arrived yet (the Market): the title on the plain
 * card, on the text grid, and two bars that breathe under it. */
static void placeholder(const menu_item_t *it, int x, int y, float t, int i)
{
    enum { LINE = 9 };                  /* characters a line in the card */
    char lines[2][LINE + 1] = { "", "" };
    const char *p = it->title ? it->title : "";
    for (int n = 0; n < 2 && *p; n++) {
        while (*p == ' ') p++;
        int len = (int)strlen(p), take = len <= LINE ? len : LINE;
        if (len > LINE)
            for (int k = LINE; k > 0; k--)
                if (p[k] == ' ') { take = k; break; }
        memcpy(lines[n], p, (size_t)take);
        lines[n][take] = 0;
        p += take;
    }
    const int tx = (x + 4 + 7) / 8 * 8;         /* on the text grid */
    for (int n = 0; n < 2; n++) {
        int ty = (y + 12 + 7) / 16 * 16 + 16 * n;
        if (lines[n][0] && ty >= GRID_TOP && ty + 16 <= GRID_BOT)
            m_text(tx, ty, lines[n], dim ? c16(0x4D4D58) : c16(C_DIM));
    }
    if (it->badge || it->busy)
        return;                         /* the badge is where the bars go */
    float k = 0.5f + 0.5f * sinf(t * 3.0f - (float)i * 0.6f);
    uint16_t bar = c16(over(0x3A3A4A, 0x5A5A70, (int)(k * 255.0f)));
    if (dim)
        bar = half(bar);
    round_rect(x + 10, y + CARD_H - 24, CARD_W - 28, 6, 3, bar);
    round_rect(x + 10, y + CARD_H - 14, (CARD_W - 28) * 3 / 5, 6, 3, bar);
}

/* A pill on the lower part of a cover ("Playing", "Installed", "42%"), on
 * the text grid; `hot`: on the accent colour. */
static void cover_badge(int x, int y, const char *text, int hot)
{
    int tx = (x + 6 + 7) / 8 * 8, ty = (y + CARD_H - 20) / 16 * 16, n = (int)strlen(text);
    if (ty < GRID_TOP || ty + 16 > GRID_BOT)
        return;
    uint32_t bg = hot ? C_ACCENT : 0x101014, fg = hot ? C_BAR : C_TEXT;
    round_rect(tx - 6, ty - 2, n * 8 + 12, 20, 10, dim ? half(c16(bg)) : c16(bg));
    m_text(tx, ty, text, dim ? c16(C_DIM) : c16(fg));
}

/* A game in memory: a star at the top right of the cover, 11x11, with a shadow. */
static void cover_star(int x, int y)
{
    static const int8_t rows[11][4] = {         /* inclusive spans a-b and c-d (c < 0: none) */
        { 5, 5, -1, 0 }, { 4, 6, -1, 0 }, { 4, 6, -1, 0 }, { 0, 10, -1, 0 }, { 1, 9, -1, 0 }, { 2, 8, -1, 0 },
        { 3, 7, -1, 0 }, { 3, 7, -1, 0 }, { 2, 8, -1, 0 }, { 2, 4, 6, 8 }, { 1, 3, 7, 9 },
    };
    const int sx = x + CARD_W - 11 - 4, sy = y + 4;
    const uint16_t fill = dim ? half(c16(0xFFD23C)) : c16(0xFFD23C), shade = c16(0x201800);
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < 11; j++) {
            const int o = pass ? 0 : 1;         /* the shadow first, 1 px down and right */
            hspan(sx + rows[j][0] + o, sx + rows[j][1] + 1 + o, sy + j + o, pass ? fill : shade);
            if (rows[j][2] >= 0)
                hspan(sx + rows[j][2] + o, sx + rows[j][3] + 1 + o, sy + j + o, pass ? fill : shade);
        }
}

/* A game with a newer version in the Market: a triangle pointing down at the
 * bottom right of the cover, 11 wide and 6 high, with a shadow. */
static void cover_update(int x, int y)
{
    const int tx = x + CARD_W - 11 - 4, ty = y + CARD_H - 6 - 4;
    const uint16_t fill = dim ? half(c16(C_ACCENT)) : c16(C_ACCENT), shade = c16(0x001018);
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < 6; j++) {
            const int o = pass ? 0 : 1;
            hspan(tx + j + o, tx + 11 - j + o, ty + j + o, pass ? fill : shade);
        }
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
    m_text(x, row * 16, s, c16(fg));
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
    if (gpu_draw) {
        for (int j = 0; j < PROMPT_H; j++) {
            for (int i = 0; i < p->w; i++) {
                uint32_t c = p->px[j * p->w + i], a = c >> 24;
                if (!a)
                    continue;
                uint16_t col = c16(over(C_BAR, c & 0xFFFFFF, (int)a));
                int i1 = i;
                while (i1 + 1 < p->w) {
                    uint32_t c1 = p->px[j * p->w + i1 + 1], a1 = c1 >> 24;
                    if (!a1)
                        break;
                    if (c16(over(C_BAR, c1 & 0xFFFFFF, (int)a1)) != col)
                        break;
                    i1++;
                }
                gpu3d_rect2d(&g, (x + i) * scale, (y + j) * scale, (x + i1 + 1) * scale, (y + j + 1) * scale, col);
                i = i1;
            }
        }
        return x + p->w;
    }
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
enum { BTN_A, BTN_B, BTN_X, BTN_CHANGE, BTN_MONITOR, BTN_Y, BTN_REFRESH };

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
        case BTN_REFRESH: p[0] = prompt_key('R'); return 1;
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
    const int rgb30 = v->prompts == MENU_PROMPTS_RGB30;  /* the console's own A B X Y */
    int ds = v->prompts != MENU_PROMPTS_PAD && !rgb30;
    if (rgb30) {                                /* the console's OK is B and its back Y unless the player set others */
        const char *face = b == BTN_A ? v->ok_btn : b == BTN_B ? v->back_btn : NULL;
        if (b == BTN_B && !face)
            face = "Y";
        if (face)
            b = face[0] == 'A' ? BTN_A : face[0] == 'B' ? BTN_B : face[0] == 'X' ? BTN_X : BTN_Y;
        else if (v->confirm_b && b == BTN_A)
            b = BTN_B;
    } else if (!ds && v->confirm_b && (b == BTN_A || b == BTN_B))
        b = b == BTN_A ? BTN_B : BTN_A;         /* confirm on B */
    switch (b) {
    case BTN_A: case BTN_B: case BTN_X:
        p[0] = ds ? prompt_get(ds4[b], v->prompts_colour)
                  : prompt_get(rgb30 ? prompt_rgb30(pad[b]) : pad[b], 0);
        return 1;
    case BTN_Y:
        p[0] = ds ? prompt_get(PROMPT_TRIANGLE, v->prompts_colour)
                  : prompt_get(rgb30 ? PROMPT_RGB30_Y : PROMPT_PAD_Y, 0);
        return 1;
    case BTN_REFRESH:
        p[0] = prompt_get(ds ? PROMPT_SHARE : PROMPT_PAD_SELECT, 0);
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
static int hint_btn(const menu_view_t *v, int col, int row, int b, const char *label, int enabled)
{
    const prompt_t *p[3] = { NULL, NULL, NULL };
    const char *join;
    int n = button_prompts(v, b, p, &join);
    int x = col * 8 - 4, y = row * 16;
    for (int i = 0; i < n; i++) {
        if (i) {
            x += 2;
            if (join[0]) {
                m_text(x, y, join, c16(enabled ? C_TEXT : C_DIM));
                x += 8 * (int)strlen(join) + 2;
            }
        }
        x = put_prompt(p[i], x, y);
    }
    int lc = (x + 4 + 7) / 8;
    m_text(lc * 8, y, label, c16(enabled ? C_TEXT : C_DIM));
    /* the pointer (M32) clicks it: A, B, X; "Change" is A on a choice row; R for refresh */
    static const char letter[] = { 'A', 'B', 'X', 'A', 0, 'Y', 'R' };
    if (!hints_dead && enabled && letter[b])
        zone(col * 8 - 8, y - 4, (lc + (int)strlen(label)) * 8 - (col * 8 - 8) + 4, 24,
             MENU_HIT_BUTTON, letter[b], 1);
    return lc + (int)strlen(label) + 3;
}

static int hint(const menu_view_t *v, int col, int row, int b, const char *label)
{
    return hint_btn(v, col, row, b, label, 1);
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

static int by_enter;                    /* Settings: Enter changes a choice (no arrows), set a frame */

/* faded: a question is over it */
/* a list of rows in a box from x, w wide: the title, MENU_PANEL_ROWS rows
 * (the selected one on a pill; faded: grey, the focus is elsewhere; sel < 0:
 * none), the line of help (p->help) at the bottom; values: the rows'
 * values (else only the ">" of a submenu) */
static void draw_list(const menu_panel_t *p, int x0, int w, int faded, int hit, int values)
{
    round_rect(x0 - 2, PANEL_Y - 2, w + 4, PANEL_H + 4, 16, c16(C_LINE));
    round_rect(x0, PANEL_Y, w, PANEL_H, 14, c16(C_BAR));
    const int col0 = x0 / 8, label_col = col0 + 3, value_end = (x0 + w) / 8 - 3;
    char buf[72];
    ksnprintf(buf, sizeof buf, "%s", p->title ? p->title : "");
    if (w / 8 - 4 < (int)sizeof buf)
        buf[w / 8 - 4] = 0;
    m_text((col0 + 2) * 8, 4 * 16, buf, c16(faded ? C_DIM : C_TEXT));
    m_rect(x0 + 16, 88, w - 32, 1, c16(C_LINE));
    for (int i = 0; i < MENU_PANEL_ROWS && p->top + i < p->n; i++) {
        const menu_row_t *r = &p->rows[p->top + i];
        int row = PANEL_ROW0 + 2 * i, y = row * 16, sel = p->top + i == p->sel;
        zone(x0 + 16, y - 8, w - 32, 32, hit, p->top + i, 1);
        if (sel)
            round_rect(x0 + 16, y - 6, w - 32, 28, 8, faded ? c16(C_LINE) : c16(C_TAB_ON));
        sel = sel && !faded;
        const int info = r->kind == MENU_ROW_INFO || r->kind == MENU_ROW_BAR;
        uint16_t fg = sel ? c16(C_BAR) : info ? c16(C_DIM) : c16(C_TEXT);
        uint16_t fv = sel ? c16(0x4A4A56) : info ? c16(C_TEXT) : c16(C_DIM);
        ksnprintf(buf, sizeof buf, "%s", r->label ? r->label : "");
        buf[30] = 0;
        if (r->swatch)
            m_rect(label_col * 8 - 12, y + 3, 8, 8, c16(r->swatch));
        m_text(label_col * 8, y, buf, fg);
        if (r->kind == MENU_ROW_BAR) {
            static const uint32_t part_col[MENU_BAR_PARTS] = { 0xE06C75, 0x61AFEF, 0x98C379, 0xC678DD, 0x56B6C2, 0xE5C07B, 0x4A5060 };  /* kernel, bm, bme, glb, bmm, other, free */
            const int bx = (label_col + (int)strlen(buf) + 1) * 8, bw = value_end * 8 - bx;
            if (r->bar && bw > 0) {
                m_rect(bx, y + 2, bw, 10, c16(C_LINE));
                int px = bx;
                for (int k = 0; k < MENU_BAR_PARTS; k++) {
                    int pw = bw * r->bar[k] / 1000;
                    if (pw > bx + bw - px)
                        pw = bx + bw - px;
                    if (pw > 0)
                        m_rect(px, y + 2, pw, 10, c16(part_col[k]));
                    px += pw;
                }
            }
            continue;
        }
        const char *val = values && r->value ? r->value : "";
        if (r->kind == MENU_ROW_CHOICE && sel && values && !by_enter)
            ksnprintf(buf, sizeof buf, "< %s >", val);
        else if (r->kind == MENU_ROW_SUB)
            ksnprintf(buf, sizeof buf, "%s%s>", val, val[0] ? "  " : "");
        else
            ksnprintf(buf, sizeof buf, "%s", val);
        int max = value_end - label_col - (int)strlen(r->label ? r->label : "") - 2;
        if (max > 34) max = 34;
        if (max < 0) max = 0;
        if ((int)strlen(buf) > max)
            buf[max] = 0;
        m_text((value_end - (int)strlen(buf)) * 8, y, buf, fv);
    }
    if (p->top > 0)
        scroll_mark(x0 + w - 12, 100, -1, c16(C_DIM));
    if (p->top + MENU_PANEL_ROWS < p->n)
        scroll_mark(x0 + w - 12, 268, 1, c16(C_DIM));
    m_rect(x0 + 16, 281, w - 32, 1, c16(C_LINE));
    if (p->help) {
        ksnprintf(buf, sizeof buf, "%s", p->help);
        if (w / 8 - 4 < (int)sizeof buf)
            buf[w / 8 - 4] = 0;
        m_text((col0 + 2) * 8, 18 * 16, buf, c16(C_DIM));
    }
}

static void draw_panel(const menu_panel_t *p, int faded)
{
    zone(PANEL_X - 2, PANEL_Y - 2, PANEL_W + 4, PANEL_H + 4, MENU_HIT_PANEL, 0, 1);
    draw_list(p, PANEL_X, PANEL_W, faded, MENU_HIT_ROW, 1);
}

/* ---------------------------------------------------------------- the Settings page */

#define C_PAGE      0x0E0E12
#define SECTIONS_W  (25 * 8)                    /* the left column on the Pi */

static void draw_page(const menu_page_t *pg, int faded)
{
    m_rect(0, BAR_H, SW, FOOT_Y - BAR_H, c16(C_PAGE));
    if (WIDE) {
        /* the help of the focused list under the rows (the wide box) */
        const int rx = 2 * 8 + SECTIONS_W + 8;
        menu_panel_t sec = *pg->sections;
        sec.help = NULL;
        draw_list(&sec, 2 * 8, SECTIONS_W, faded || pg->focus != 0,
                  pg->focus ? MENU_HIT_SECTION : MENU_HIT_ROW, 0);
        if (pg->rows) {
            menu_panel_t rows = *pg->rows;
            if (!pg->focus)
                rows.help = pg->sections->help;
            zone(rx - 2, PANEL_Y - 2, SW - 2 * 8 - rx + 4, PANEL_H + 4, MENU_HIT_PANEL, 0, 1);
            draw_list(&rows, rx, SW - 2 * 8 - rx, faded || pg->focus != 1,
                      pg->focus ? MENU_HIT_ROW : MENU_HIT_PANEL, 1);
        }
    } else {
        const menu_panel_t *p = pg->focus && pg->rows ? pg->rows : pg->sections;
        draw_list(p, PANEL_X, PANEL_W, faded, MENU_HIT_ROW, 1);
    }
}

/* an icon of icons.c over the bar: the icon in `ink`, the number's disc in
 * `disc` with the digit in `digit`; only written */
static void put_icon(const icon_mask_t *m, int x0, int y0, uint32_t ink, uint32_t disc, uint32_t digit)
{
    if (gpu_draw) {
        for (int y = 0; y < ICON_BH; y++) {
            for (int x = 0; x < ICON_W; x++) {
                int i = y * ICON_W + x;
                if (!m->icon[i] && !m->disc[i])
                    continue;
                uint32_t c = m->digit[i] ? digit : over(over(C_BAR, ink, m->icon[i]), disc, m->disc[i]);
                uint16_t col = c16(c);
                int x1 = x;
                while (x1 + 1 < ICON_W) {
                    int i1 = y * ICON_W + x1 + 1;
                    if (!m->icon[i1] && !m->disc[i1])
                        break;
                    uint32_t c1 = m->digit[i1] ? digit : over(over(C_BAR, ink, m->icon[i1]), disc, m->disc[i1]);
                    if (c16(c1) != col)
                        break;
                    x1++;
                }
                gpu3d_rect2d(&g, (x0 + x) * scale, (y0 + y) * scale, (x0 + x1 + 1) * scale, (y0 + y + 1) * scale, col);
                x = x1;
            }
        }
        return;
    }
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

/* right-aligned: a keyboard or a controller for each player, then the
 * mice, all without a number (a blue dot on Bluetooth), then WiFi or
 * Ethernet when the console is on a network, and the battery (the RGB30:
 * four bars, the last from 75%; red under 10%, unless on the charger) */
#define C_LOW       0xFF5A5A
#define BATTERY     99              /* not an icon of icon_mask: icon_battery */

/* draws them; returns where they begin (SW: none) */
static int status_icons(const menu_view_t *v)
{
    int icon[8], num[8], bt[8], n = 0;
    for (int p = 0; p < 4; p++)
        if (v->dev[p] != MENU_DEV_NONE) {
            icon[n] = v->dev[p] == MENU_DEV_KEYBOARD ? ICON_KEYBOARD : ICON_PAD;
            bt[n] = v->bt >> p & 1;
            num[n] = bt[n] ? ICON_DOT : 0;      /* no number: Bluetooth only its blue dot */
            n++;
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
    if (v->battery) {
        icon[n] = BATTERY;
        bt[n] = 0;
        num[n++] = 0;
    }
    const int gap = 8, net_gap = 16, y0 = 12;
    int w = n * ICON_W + (n > 1 ? (n - 1) * gap : 0) + (players && players < n ? net_gap - gap : 0);
    int x = SW - 16 - w;
    const int pct = v->battery_pct, low = pct < 10 && !v->charging, left = n ? x : SW;
    for (int i = 0; i < n; i++) {
        if (icon[i] == BATTERY)
            put_icon(icon_battery(pct >= 75 ? 4 : pct >= 50 ? 3 : pct >= 25 ? 2 : pct >= 10 ? 1 : 0, v->charging),
                     x, y0, low ? C_LOW : C_TEXT, C_TEXT, C_BAR);
        else {
            const icon_mask_t *m = icon_mask(icon[i], num[i]);
            if (m)                  /* USB: the icon alone; Bluetooth: its blue dot */
                put_icon(m, x, y0, i >= players && v->net_wait ? C_DIM : C_TEXT,
                         bt[i] ? C_BT : C_TEXT, bt[i] ? C_TEXT : C_BAR);
        }
        x += ICON_W + (i + 1 == players ? net_gap : gap);
    }
    return left;
}

/* ---------------------------------------------------------------- F12: the keys */

/* the menu's own keys on the keyboard (the system's are in syskeys.c) */
static const struct { const char *keys, *what; } menu_keys[] = {
    { "up down left right", "choose (or w a s d)" },
    { "enter", "play, open, yes" },
    { "c", "options of the game" },
    { "v", "Market: Apps or Games" },
    { "q / e", "the tab before, after" },
    { "tab", "the next tab" },
    { "1 - 4", "Market ... Settings" },
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
            m_text(x + 2, row * 16, tok, c16(C_DIM));
            x += 8 * (int)strlen(tok) + 4;
        }
    }
    return x;
}

/* While F12 is held: the system's keys, then the menu's, with their
 * pictures, over everything (columns on the text grid) */
static void keys_help(void)
{
    m_rect(0, 0, SW, SH, c16(C_BAR));
    m_text(2 * 8, 0, "Keys", c16(C_ACCENT));
    m_text(7 * 8, 0, "(F12 held)", c16(C_DIM));
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
            m_text(col * 8, row * 16, i == 0 ? "bm" : "Menu", c16(0xFFB040));
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
        m_text(tc * 8, row * 16, buf, c16(C_TEXT));
    }
}

/* ---------------------------------------------------------------- screen */

int menu_ui_cols(void)
{
    return cols;
}

/* the screen of the layout: on the RGB30 360x360 shown 2x (the display
 * controller enlarges it); on the Pi 640x360 drawn by the ARM (the HDMI
 * scaler enlarges it). menu_scale in bm/config.txt (Settings > Screen > Menu size) asks for more:
 * 2 is 1280x720 and 3 is 1920x1080, both drawn by the GPU at their own size; 4 is 640x360 drawn by the
 * GPU. The ARM never enlarges the menu: when the GPU cannot draw it, the menu stays at 360p on the ARM. */
static int screen_open(framebuffer_t *fb)
{
#ifdef BM_RGB30
    sw = 360;
    scale = 1;
    gpu_draw = 0;
    gpu_big = 0;
    return fb_init_mode(fb, (uint32_t)sw, (uint32_t)sh, 3, 16, 2, 0);
#else
    const char *ms = config_get("menu_scale");
    sw = 640;
    gpu_draw = 0;
    gpu_big = 0;
    scale = ms && ms[0] == '3' ? 3 : ms && ms[0] == '2' ? 2 : 1;
    if (scale > 1 || (ms && ms[0] == '4')) {        /* 4: 360p drawn by the GPU */
        if (fb_init_depth(fb, (uint32_t)(sw * scale), (uint32_t)(sh * scale), 3, 16) == 0 &&
            fb->pitch == fb->width * 2 && gpu3d_init() == 0) {
            gpu3d_set_fb(fb->mem, fb->size, fb->bus);
            gpu3d_set_size((int)fb->width, (int)fb->height);
            gpu_draw = 1;
            kprintf("menu: %lux%lu, drawn by the GPU\n", (unsigned long)fb->width, (unsigned long)fb->height);
            return 0;
        }
        if (scale > 1)
            kprintf("menu: the GPU cannot draw %dx, 360p on the ARM\n", scale);
        else
            kprintf("menu: the GPU cannot draw 360p, the ARM does\n");
        scale = 1;
    }
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
    if (gpu_draw) {
        if (!bg_sheet_alloced) {
            if (g16_sheet_alloc(&bg_sheet, SW, SH) == 0) {
                memset(bg_sheet.alpha, 1, (size_t)bg_sheet.w * bg_sheet.h);
                bg_sheet_alloced = 1;
            }
        }
    }
    cols = (SW - 16) / (CARD_W + GAP_X);    /* 6 at 640, 3 at 360 */
    ready = 1;
    bg_valid = 0;
    bg_building = 0;
    fade = 0;
    nzones = 0;
    pointer_env(1, SW, SH);
    t0 = timer_ticks();
    deadline = t0 + FRAME_US;
    frame_end = t0;
    idle_first = 1;
    return 0;
}

static void close_menu(framebuffer_t *fb, int console)
{
    if (!ready)
        return;
    ready = 0;
    nzones = 0;
    pointer_env(0, 0, 0);
    if (bg_sheet_alloced) {
        g16_sheet_free(&bg_sheet);
        bg_sheet_alloced = 0;
    }
    if (gpu_draw || gpu_big) {
        gpu3d_drop();
        gpu_draw = gpu_big = 0;
    }
    fb_init(fb, con_w, con_h, 2);
    if (console)
        console_suspend(0);
}

void menu_ui_close(framebuffer_t *fb)
{
    close_menu(fb, 1);
}

void menu_ui_close_quiet(framebuffer_t *fb)
{
    close_menu(fb, 0);
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

/* menu_gpu=1: the first frames, what the GPU wrote against the pixels the ARM would copy: how many are wrong,
 * by row of the block (y % scale), by column of the block (x % scale) and by row parity of the layout, the
 * first wrong one and the colours of the first row; then the ARM draws the frame as it should be, so the
 * menu stays readable (the stripes of the Pi, 24 Oct: the line tells which kind they are) */
#define GPU_CHECKS 3
static int gpu_checks;

static void gpu_check(framebuffer_t *fb)
{
    gpu3d_sync();                       /* the job is over before the ARM reads the page */
    const uint32_t stride = fb->pitch / 2;
    const uint16_t *page = (const uint16_t *)fb->base;
    uint32_t bad = 0, by_row[3] = { 0, 0, 0 }, by_col[3] = { 0, 0, 0 }, by_par[2] = { 0, 0 };
    int fx = -1, fy = -1;
    uint16_t got = 0, want = 0;
    for (int y = 0; y < SH * scale; y++)
        for (int x = 0; x < SW * scale; x++) {
            const uint16_t w = frame[(y / scale) * SW + x / scale], v = page[(uint32_t)y * stride + (uint32_t)x];
            if (v == w)
                continue;
            if (!bad) {
                fx = x; fy = y; got = v; want = w;
            }
            bad++;
            by_row[y % scale]++;
            by_col[x % scale]++;
            by_par[(y / scale) & 1]++;
        }
    gpu_checks++;
    kprintf("menu: GPU check %d: %lu of %lu pixels differ; by row of the block %lu/%lu/%lu, by column %lu/%lu/%lu, "
            "even/odd layout rows %lu/%lu; first at %d,%d got %04x want %04x; pitch %lu, width %lu\n", gpu_checks,
            (unsigned long)bad, (unsigned long)(SW * scale) * (unsigned long)(SH * scale), (unsigned long)by_row[0],
            (unsigned long)by_row[1], (unsigned long)by_row[2], (unsigned long)by_col[0], (unsigned long)by_col[1],
            (unsigned long)by_col[2], (unsigned long)by_par[0], (unsigned long)by_par[1], fx, fy, got, want,
            (unsigned long)fb->pitch, (unsigned long)fb->width);
    if (bad)
        enlarge(fb);
}


/* the long calls that hold the CPU (stalls.h), a line when one ran in the window: count,
 * the last and the longest in ms; the hash adds the KB. Starts counting again. */
static void stall_print(const char *name, stall_t *s, char *buf, size_t n, size_t *k)
{
    if (!s->n)
        return;
    if (*k < n)
        *k += (size_t)ksnprintf(buf + *k, n - *k, "%s%s %lux last %lu.%lu worst %lu.%lu ms", *k ? ", " : "", name,
                                (unsigned long)s->n, (unsigned long)(s->last / 1000),
                                (unsigned long)(s->last / 100 % 10), (unsigned long)(s->worst / 1000),
                                (unsigned long)(s->worst / 100 % 10));
    if (s->bytes && *k < n)
        *k += (size_t)ksnprintf(buf + *k, n - *k, " (%lu KB)", (unsigned long)(s->bytes / 1024));
    *s = (stall_t){0};
}

static void stalls_report(void)
{
    char line[160];
    size_t k = 0;
    stall_print("tls handshake", &stall_tls, line, sizeof line, &k);
    stall_print("verify", &stall_verify, line, sizeof line, &k);
    stall_print("file hash", &stall_hash, line, sizeof line, &k);
    if (k)
        kprintf("menu stalls: %s\n", line);
}

/* Where the frame is drawn: the panel, or the layout that is enlarged after. */
static void frame_target(framebuffer_t *fb)
{
    if (gpu_draw) {
        g16_target(&g, (uint16_t *)fb->base, fb->pitch / 2, SW * scale, SH * scale, menu_font(scale));
        gpu3d_drop();
        gpu3d_page(1, 0);
        m_clip(0, 0, 0, 0);
    } else if (scale > 1) {
        g16_target(&g, frame, (uint32_t)SW, SW, SH, &font_console_8x16);
    } else {
        g16_target(&g, (uint16_t *)fb->base, fb->pitch / 2, SW, SH, &font_console_8x16);
    }
}

/* The background: the selected cover, blurred, and the cross-fade. */
static void frame_background(const menu_item_t *cur)
{
    /* background: the selected cover, blurred; a cross-fade on change. It is
     * made BG_ROWS rows per frame while the old one stays on screen: the
     * whole picture in one frame was 3 frames or more on the Pi, and the
     * pointer jumped each time it moved onto another cover. */
    const g16_sheet_t *key = cur ? cur->cover : NULL;
    const uint16_t *kpx = key && key->px && key->w >= 16 && key->h >= 10 ? key->px : NULL;
    if (!bg_valid) {
        menu_bg_begin(&bgb, kpx, key ? key->w : 0, key ? key->h : 0, SW, SH);
        menu_bg_step(&bgb, bg_cur, SH);         /* the first one: nothing to show meanwhile */
        bg_key = key;
        bg_valid = 1;
        bg_building = 0;
    } else if (key != bg_key) {
        if (!bg_building) {                     /* the picture on screen is kept in bg_prev */
            uint16_t *s = bg_prev; bg_prev = bg_cur; bg_cur = s;
        }
        fade = 0;
        bg_building = 1;
        menu_bg_begin(&bgb, kpx, key ? key->w : 0, key ? key->h : 0, SW, SH);
        bg_key = key;
    }
    if (bg_building && menu_bg_step(&bgb, bg_cur, BG_ROWS)) {
        bg_building = 0;
        fade = FADE_FRAMES;
    }
    put_background();
}

/* The grid of covers (or the Settings page), with the pointer zones. */
static void frame_grid(const menu_view_t *v, float t)
{
    /* grid: keep the selected row among the two fully visible ones */
    int sel_row = v->sel / cols;
    if (sel_row < first_row) first_row = sel_row;
    if (sel_row > first_row + 1) first_row = sel_row - 1;
    if (first_row < 0) first_row = 0;
    scroll += ((float)first_row - scroll) * 0.25f;
    if (fabsf(scroll - (float)first_row) < 0.01f) scroll = (float)first_row;

    m_clip(0, GRID_TOP, SW, GRID_BOT - GRID_TOP);
    for (int i = 0; !v->page && i < v->n; i++) {
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
            round_ring(x - 3, y - 3, CARD_W + 6, CARD_H + 6, RADIUS, 3,
                       dim ? c16(0x45454B) : pulse(t));
        const menu_item_t *it = &v->items[i];
        card(it->cover, x, y, it->grey);
        if (it->loading && !it->cover)
            placeholder(it, x, y, t, i);
        if (it->busy) {
            char pc[8];
            ksnprintf(pc, sizeof pc, "%d%%", it->percent);
            cover_badge(x, y, pc, 1);
            cover_progress(x, y, it->percent);
        } else if (it->badge) {
            cover_badge(x, y, it->badge, 0);
        }
        if (it->running)
            cover_star(x, y);
        if (it->update)
            cover_update(x, y);
    }
    m_clip(0, 0, 0, 0);
    if (!v->page && v->n > 2 * cols)
        scroll_bar((v->n + cols - 1) / cols, scroll);
    if (v->page)
        draw_page(v->page, v->ask != NULL);
}

/* The top bar: the tabs and Settings. */
static void frame_tabs(const menu_view_t *v)
{
    /* top bar: the tabs and Settings (the last tab: its panel; L1 / R1 move
     * between them), then the players and the network */
    m_rect(0, 0, SW, BAR_H, c16(C_BAR));
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
    /* the tabs stop short of the icons (the RGB30's narrow bar: Settings
     * peeks at the right while the Market is the tab), closer there */
    const int icons_x = status_icons(v), sep = SW < 480 ? 3 : 4;
    m_clip(0, 0, icons_x - 8, BAR_H);
    int col = 3;
    for (int i = 0; i < v->ntabs; i++) {
        int n = (int)strlen(v->tabs[i]), x = col * 8 + ox;
        int zx = x - 8 < 0 ? 0 : x - 8, zw = x - 8 + (n + 2) * 8 - zx;
        zone(zx, 4, zw, 40, MENU_HIT_TAB, i, 1);
        if (i == v->tab && !v->on_gear)
            pill_at(x, 1, v->tabs[i], C_BAR, C_TAB_ON);
        else
            m_text(x, 16, v->tabs[i], c16(C_DIM));
        col += n + sep;
    }
    zone(col * 8 + ox - 8, 4, 10 * 8, 40, MENU_HIT_SETTINGS, 0, 1);
    if (v->on_gear)
        pill_at(col * 8 + ox, 1, "Settings", C_BAR, C_TAB_ON);
    else
        m_text(col * 8 + ox, 16, "Settings", c16(C_DIM));
    m_clip(0, 0, 0, 0);
}

/* The name of the selected cartridge, and the sub tabs at the right. */
static void frame_title(const menu_view_t *v, const menu_item_t *cur)
{
    int sub_w = 0;
    if (v->subtabs && v->nsubtabs > 0) {
        for (int i = 0; i < v->nsubtabs; i++)
            sub_w += (int)strlen(v->subtabs[i]) + (i + 1 < v->nsubtabs ? 3 : 0);
        sub_w += 3;
        if (v->refresh_label && v->refresh_label[0])
            sub_w += (int)strlen(v->refresh_label) + 4;
        int scol = SW / 8 - sub_w;
        round_rect((scol - 2) * 8, TITLE_ROW * 16 - 6, (sub_w + 1) * 8, 28, 14, c16(C_BAR));
        for (int i = 0; i < v->nsubtabs; i++) {
            int sn = (int)strlen(v->subtabs[i]);
            zone(scol * 8 - 8, TITLE_ROW * 16 - 6, (sn + 2) * 8, 28, MENU_HIT_GROUP, i, 1);
            if (i == v->subtab)
                pill_text(scol, TITLE_ROW, v->subtabs[i], C_BAR, C_TAB_ON);
            else
                m_text(scol * 8, TITLE_ROW * 16, v->subtabs[i], c16(C_DIM));
            scol += sn + 3;
        }
        if (v->refresh_label && v->refresh_label[0]) {
            int rn = (int)strlen(v->refresh_label);
            zone(scol * 8 - 8, TITLE_ROW * 16 - 6, (rn + 2) * 8, 28, MENU_HIT_BUTTON, 'R', v->refresh_enabled);
            if (v->refresh_enabled)
                pill_text(scol, TITLE_ROW, v->refresh_label, C_BAR, C_ACCENT);
            else
                m_text(scol * 8, TITLE_ROW * 16, v->refresh_label, c16(C_DIM));
        }
    }
    int max_cols = SW / 8 - 6 - sub_w;
    if (max_cols < 12)
        max_cols = 12;
    if (v->banner) {
        char buf[72];
        ksnprintf(buf, sizeof buf, "%s", v->banner);
        if (max_cols < (int)sizeof buf)
            buf[max_cols] = 0;
        pill_text(3, TITLE_ROW, buf, C_DIM, C_PILL);
    } else if (cur && cur->title && cur->title[0]) {
        char buf[72];
        ksnprintf(buf, sizeof buf, "%s", cur->title);
        if (max_cols < (int)sizeof buf)
            buf[max_cols] = 0;
        int n = (int)strlen(buf);
        round_rect(3 * 8 - 10, TITLE_ROW * 16 - 6, (n + 2) * 8 + 4, 28, 14, c16(C_LINE));
        pill_text(3, TITLE_ROW, buf, C_TEXT, C_PILL);
    } else if (v->n == 0) {
        pill_text(3, TITLE_ROW, "nothing here yet", C_DIM, C_PILL);
    }
}

/* The bottom bar: the key chips. */
static void frame_hints(const menu_view_t *v, const menu_item_t *cur)
{
    int col;
    /* bottom bar: the key chips only (2026-10-25, the user's request); what
     * went wrong goes to the notice box, the rest to the console log */
    hints_dead = v->ask != NULL;
    m_rect(0, FOOT_Y, SW, SH - FOOT_Y, c16(C_BAR));
    m_rect(16, FOOT_Y + 1, SW - 32, 1, c16(C_LINE));
    const int hc = WIDE ? 32 : 2;               /* where the hints start */
    /* the hints of a panel, or of the Settings page's focused list */
    const menu_panel_t *hp = v->panel ? v->panel
                           : v->page ? (v->page->focus && v->page->rows ? v->page->rows : v->page->sections) : NULL;
    if (hp) {
        const menu_row_t *r = hp->sel >= 0 && hp->sel < hp->n ? &hp->rows[hp->sel] : NULL;
        col = WIDE ? 40 : hc;
        if (r && r->kind == MENU_ROW_CHOICE)
            col = hint(v, col, 21, v->on_gear ? BTN_A : BTN_CHANGE, "Change");
        else if (r && r->kind != MENU_ROW_INFO && r->kind != MENU_ROW_BAR)
            col = hint(v, col, 21, BTN_A, r->kind == MENU_ROW_SUB ? "Open" : "Select");
        hint(v, WIDE && col < 55 ? 55 : col, 21, BTN_B, "Back");
    } else if (v->on_gear) {
        col = hint(v, hc, 21, BTN_A, "Settings");
        if (!v->no_monitor)
            hint(v, col, 21, BTN_MONITOR, "Monitor");
    } else {
        const char *a = v->a_label ? v->a_label
                      : cur && cur->kind && (!strcmp(cur->kind, "tool") || !strcmp(cur->kind, "bme")) ? "Open"
                                                                                                          : "Play";
        col = hc;
        if (a[0])
            col = hint(v, col, 21, BTN_A, a);
        if (cur && cur->kind && strcmp(cur->kind, "market") == 0 && cur->title && cur->title[0])
            col = hint(v, col, 21, BTN_X, "Details");
        else if (v->n && v->items[v->sel].path && v->items[v->sel].path[0])
            col = hint(v, col, 21, BTN_X, "Options");
        if (v->y_label && v->y_label[0])
            col = hint(v, col, 21, BTN_Y, v->y_label);
        if (v->refresh_label && v->refresh_label[0])
            col = hint_btn(v, col, 21, BTN_REFRESH, v->refresh_label, v->refresh_enabled);
        if (!v->no_monitor && (!WIDE || col <= (v->prompts == MENU_PROMPTS_KEYBOARD ? 60 : 66)))
            hint(v, col, 21, BTN_MONITOR, "Monitor");
    }
}

/* A question in a panel over everything. */
static void frame_ask(const menu_view_t *v)
{
    const int px = WIDE ? 12 * 8 : 2 * 8, py = 8 * 16 - 8, pw = SW - 2 * px, ph = 5 * 16 + 16;
    round_rect(px - 2, py - 2, pw + 4, ph + 4, 14, c16(C_LINE));
    round_rect(px, py, pw, ph, 12, c16(C_BAR));
    zone(px - 2, py - 2, pw + 4, ph + 4, MENU_HIT_ASK, 0, 1);
    char q[64];
    ksnprintf(q, sizeof q, "%s", v->ask);
    q[pw / 8 - 2] = 0;
    m_text((SW / 8 - (int)strlen(q)) / 2 * 8, 9 * 16, q, c16(C_TEXT));
    if (v->ask_detail) {
        ksnprintf(q, sizeof q, "%s", v->ask_detail);
        q[pw / 8 - 2] = 0;
        m_text((SW / 8 - (int)strlen(q)) / 2 * 8, 10 * 16, q, c16(C_DIM));
    }
    int c = hint(v, WIDE ? 26 : 4, 12, BTN_A, v->ask_yes ? v->ask_yes : "Close it");
    hint(v, c + 2, 12, BTN_B, "Cancel");
}

/* The system's notice, over everything (a kernel arriving, a restart). */
static void frame_notice(const menu_view_t *v)
{
    const int px = WIDE ? 12 * 8 : 2 * 8, py = 8 * 16 - 8, pw = SW - 2 * px, ph = 5 * 16 + 16;
    round_rect(px - 2, py - 2, pw + 4, ph + 4, 14, c16(C_ACCENT));
    round_rect(px, py, pw, ph, 12, c16(C_BAR));
    char q[64];
    ksnprintf(q, sizeof q, "%s", v->notice);
    q[pw / 8 - 2] = 0;
    m_text((SW / 8 - (int)strlen(q)) / 2 * 8, 9 * 16, q, c16(C_TEXT));
    if (v->notice_detail) {
        ksnprintf(q, sizeof q, "%s", v->notice_detail);
        q[pw / 8 - 2] = 0;
        m_text((SW / 8 - (int)strlen(q)) / 2 * 8, 10 * 16, q, c16(C_DIM));
    }
    if (v->notice_progress >= 0) {
        const int bx = px + 16, bw = pw - 32, by = 12 * 16 + 4, fill = bw * v->notice_progress / 1000;
        round_rect(bx, by, bw, 8, 4, c16(C_LINE));
        if (fill > 8)
            round_rect(bx, by, fill, 8, 4, c16(C_ACCENT));
    }
}

/* The log line of the menu's free time and frame times, every IDLE_FRAMES frames. */
static void frame_idle_report(int gave, int paced, int32_t slack)
{
    /* the background's time (fiber.h), a line in the log every IDLE_FRAMES
     * frames when a job ran or more than one frame had no free time: the
     * frames with some, the ms taken of the ms offered, the most past a
     * frame's end, then each job (fiber_jobs_report); min slack: the least
     * the timer path had left after a flip, over those frames (it replaces
     * the probe of the RGB30 Market that never loaded, 2026-10-10). What
     * the device says here is what to measure. One frame short is the
     * line's own (9-15 ms of UART): it no longer brings the next one every
     * 5 s with nothing running (2026-10-11). The SD card's retries and
     * timeouts of the window (sd_counters), when there were some, end it
     * (and print it: each timeout is half a second of the card). */
    idle_gave += gave;
    if (idle_first)
        idle_first = 0;
    else if (slack < idle_slack)
        idle_slack = slack;
    if (++idle_frames >= IDLE_FRAMES) {
        char jobs[384], sd[48] = "";
        const int ran = fiber_jobs_report(jobs, sizeof jobs);
        uint32_t retries, timeouts;
        sd_counters(&retries, &timeouts);
        if (retries != idle_sd_retries || timeouts != idle_sd_timeouts)
            ksnprintf(sd, sizeof sd, "; sd %lu retried, %lu timed out", (unsigned long)(retries - idle_sd_retries),
                      (unsigned long)(timeouts - idle_sd_timeouts));
        idle_sd_retries = retries;
        idle_sd_timeouts = timeouts;
        if (ran || idle_frames - idle_gave > 1 || sd[0])
            kprintf("menu idle: %d/%d frames, %lu of %lu ms taken, %lu.%lu ms past a frame; %s; "
                    "min slack %d us, paced %d%s\n", idle_gave, idle_frames, (unsigned long)(idle_used / 1000),
                    (unsigned long)(idle_offered / 1000), (unsigned long)(idle_over / 1000),
                    (unsigned long)(idle_over / 100 % 10), ran ? jobs : "no job", (int)idle_slack, paced, sd);
        {
            char st[160];
            if (sd_stats_line(st, sizeof st))
                kprintf("menu idle: %s\n", st);
        }
        stalls_report();
        kprintf("menu frame: %dx, a frame averages draw %lu us, enlarge %lu us, flip %lu us, free time used %lu us "
                "(%d frames)\n", scale, (unsigned long)(st_draw / (uint32_t)idle_frames),
                (unsigned long)(st_enlarge / (uint32_t)idle_frames), (unsigned long)(st_flip / (uint32_t)idle_frames),
                (unsigned long)(idle_used / (uint32_t)idle_frames), idle_frames);
        st_draw = st_enlarge = st_flip = 0;
        idle_frames = idle_gave = 0;
        idle_offered = idle_used = idle_over = 0;
        idle_slack = SLACK_NONE;
    }
}

/* The pointer over everything, and the layout enlarged to the panel's size. */
static void frame_show(framebuffer_t *fb)
{
    if (gpu_draw) {
        gpu3d_flush(&g, 0);
        m_pointer(fb);
    } else {
        pointer_draw(g.px, g.stride, SW, SH);       /* the arrow over everything */
        if (scale > 1) {
            g16_t page;
            g16_target(&page, (uint16_t *)fb->base, fb->pitch / 2, SW * scale, SH * scale, &font_console_8x16);
            if (!gpu_big || gpu3d_enlarge(frame, SW, SH, scale, &page) != 0) {
                if (gpu_big)
                    kprintf("menu: the GPU did not enlarge the layout (%s): the ARM does\n", gpu3d_status());
                gpu_big = 0;
                enlarge(fb);
            } else if (gpu_checks < GPU_CHECKS) {
                gpu_check(fb);              /* menu_gpu=1: what the GPU wrote, against the ARM's copy (a log line) */
            }
        }
    }
}

/* The flip, the time the frame took, the free time for the jobs (v->idle), the wait for the next frame. `pre`:
 * when the drawing ended. */
static void frame_flip_and_wait(framebuffer_t *fb, const menu_view_t *v, uint32_t pre)
{
    const uint32_t drawn = timer_ticks();
    int paced = fb_flip(fb);
#ifndef BM_RGB30
    paced = 0;                          /* the Pi keeps its timer pacing, as it was */
#endif
    const uint32_t flipped = timer_ticks();
    if (gpu_draw) {
        st_draw += drawn - frame_end;
        st_enlarge += 0;
    } else {
        st_draw += pre - frame_end;
        st_enlarge += drawn - pre;
    }
    st_flip += flipped - drawn;
    const int32_t slack = (int32_t)(deadline - 1500 - flipped);
    uint32_t until = 0;
    int gave = 0;
    if (paced) {
        /* RGB30: the flip waited for the panel's frame start (16,675 us, a
         * hair longer than FRAME_US), so deadline falls behind it and the
         * slack below never comes: the Market never loaded. The frame is the
         * panel's: free time up to the next flip, less this frame's drawing
         * (from the end of the last one to the flip) and the margin; no wait */
        const int32_t work = (int32_t)(drawn - frame_end);
        until = flipped + FRAME_US - (uint32_t)work - 1500;
        gave = v->idle && work >= 0 && (int32_t)(until - flipped) > 0;
    } else {
        /* what is left of the frame: the Market's work, if any (a little
         * margin for the flip), then the wait */
        until = deadline - 1500;
        gave = v->idle && (int32_t)(until - timer_ticks()) > 0;
        if (v->idle && !gave) {
            /* a frame that took its whole time (menu_scale 2 or 3: the ARM enlarges each frame) left no time:
             * the Market and the covers never ran (Pi log of 25 Oct: 0/300 frames given). A late frame gives the
             * jobs a small slice anyway; the menu drops frames, the work goes on. */
            until = timer_ticks() + IDLE_MIN_US * (uint32_t)(scale > 1 ? scale - 1 : 1);
            gave = 1;
        }
    }
    if (gave) {
        const uint32_t from = timer_ticks();
        v->idle(until);
        const uint32_t to = timer_ticks();
        idle_offered += until - from;
        idle_used += to - from;
        if ((int32_t)(to - until) > (int32_t)idle_over)
            idle_over = to - until;
    }
    if (!paced)
        while ((int32_t)(timer_ticks() - deadline) < 0)
            ;
    frame_idle_report(gave, paced, slack);
    uint32_t now = timer_ticks();
    deadline += FRAME_US;
    if ((int32_t)(now - deadline) > 0)
        deadline = now + FRAME_US;
    frame_end = now;
}

/* when the selection or the tab last changed (menu_ui_still); 0: not yet */
static uint32_t moved_at;
static int moved_sel = -1, moved_tab = -1;

int menu_ui_still(uint32_t us)
{
    return !moved_at || (uint32_t)(timer_ticks() - moved_at) >= us;
}

void menu_ui_frame(framebuffer_t *fb, const menu_view_t *v)
{
    by_enter = v->on_gear;
    if (!ready)
        return;
    if (v->sel != moved_sel || v->tab != moved_tab) {
        moved_sel = v->sel;
        moved_tab = v->tab;
        moved_at = timer_ticks() | 1;           /* 0 means "never moved" */
    }
    frame_target(fb);
    float t = (float)(timer_ticks() - t0) * 1e-6f;
    const menu_item_t *cur = v->sel >= 0 && v->sel < v->n ? &v->items[v->sel] : NULL;
    dim = v->panel != NULL;
    nzones = 0;

    frame_background(cur);
    frame_grid(v, t);
    frame_tabs(v);
    if (!v->panel && !v->page)
        frame_title(v, cur);                    /* (a panel covers it) */
    frame_hints(v, cur);
    hints_dead = 0;
    if (v->panel)
        draw_panel(v->panel, v->ask != NULL);

    if (v->ask)
        frame_ask(v);
    if (v->notice)
        frame_notice(v);
    if (v->keys_help)
        keys_help();
    const uint32_t pre = timer_ticks();
    frame_show(fb);
    frame_flip_and_wait(fb, v, pre);
}

/* The pointer over the menu: moving over a cover or a row selects it, the left button does what A does there
 * (or what is clicked: a tab, a button of the hints), the right one goes back, or opens the options of a cover;
 * outside a panel or a question it closes them; the wheel moves by rows. */
int menu_ui_pointer(menu_ptr_t *io)
{
    const pointer_t *pt = pointer_update();
    io->dy = io->action = io->back = io->opts = io->ybtn = io->refresh = io->settings = 0;
    io->tabto = io->panel_sel = io->section = io->cover_sel = io->page = -1;
    if (!io->gfx || !pt->shown)
        return 0;
    const menu_hit_t h = menu_ui_hit(pt->x, pt->y);
    const int click = pt->pressed & 1, right = pt->pressed & 2;
    io->dy = -pt->wheel;
    if (io->ask) {
        if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
            io->action = 1;
        else if (click && h.kind == MENU_HIT_BUTTON)
            io->back = 1;
        else if ((click && h.kind != MENU_HIT_ASK) || right)
            io->back = 1;
    } else if (io->panel) {
        if (h.kind == MENU_HIT_ROW && h.index < io->panel_rows && (pt->moved || click)) {
            io->panel_sel = h.index;
            io->action = click;
        } else if (click && h.kind == MENU_HIT_SECTION && io->on_gear) {
            io->section = h.index;              /* another section of the Settings page */
            io->action = 1;
        } else if (click && h.kind == MENU_HIT_BUTTON) {
            io->action = h.index == 'A';
            io->back = h.index == 'B';
        } else if (click && h.kind == MENU_HIT_TAB) {
            io->tabto = h.index;
        } else if (click && h.kind == MENU_HIT_SETTINGS) {
            io->settings = 1;
        } else if ((click && h.kind != MENU_HIT_PANEL && h.kind != MENU_HIT_ROW) || right) {
            io->back = 1;
        }
    } else {
        if (h.kind == MENU_HIT_COVER && h.index < io->covers && ((pt->moved && h.full) || click || right))
            io->cover_sel = h.index;
        if (click && h.kind == MENU_HIT_COVER && h.full)
            io->action = 1;
        else if (click && h.kind == MENU_HIT_GROUP && io->market) {
            if (h.index != io->market_page)
                io->page = h.index;
        } else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'Y')
            io->ybtn = 1;
        else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'R')
            io->refresh = io->market;
        else if (click && h.kind == MENU_HIT_TAB)
            io->tabto = h.index;
        else if (click && h.kind == MENU_HIT_SETTINGS)
            io->settings = 1;
        else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'A')
            io->action = 1;
        else if (click && h.kind == MENU_HIT_BUTTON && h.index == 'X')
            io->opts = 1;
        else if (right && h.kind == MENU_HIT_COVER)
            io->opts = 1;
    }
    return 1;
}
