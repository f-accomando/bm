/*
 * Shell SDK: the pixel canvas of the Sprite tab (see shell_sdk_canvas.h). Original code.
 */
#include "shell_sdk_canvas.h"

#include <string.h>

#include "shell_sdk_sheet.h"
#include "shell_sdk_tiles.h"

static sdk_canvas_t cv;
static int project_fmt = -1;
static uint8_t clip[16 * 16];               /* the pixels copied */
static int clip_w, clip_h;
static unsigned clip_when;                  /* sdk_clip_stamp() at the copy of pixels */

void sdk_canvas_reset(void)
{
    memset(&cv, 0, sizeof cv);
    cv.size = 8;
    cv.colour = 1;
    project_fmt = -1;
    clip_w = clip_h = 0;
}

const sdk_canvas_t *sdk_canvas(void) { return &cv; }

/* The tile of the sheet under a pixel of the canvas, and the pixel inside it. */
static const sdk_tile_t *tile_at(int x, int y, int *r, int *c, int *k)
{
    *r = cv.row + y / 8;
    *c = cv.col + x / 8;
    *k = (y % 8) * 8 + x % 8;
    return sdk_sheet_tile(*r, *c);
}

void sdk_canvas_size(int size)
{
    if (cv.open)
        sdk_canvas_open(cv.row, cv.col, size);
    else if (size == 8 || size == 16)
        cv.size = size;
}

int sdk_canvas_colours(void)
{
    if (project_fmt >= 0)
        return sdk_tile_colors(project_fmt);
    int n = 0;                              /* the richest of the tiles of the canvas */
    for (int y = 0; cv.open && y < cv.size; y += 8)
        for (int x = 0; x < cv.size; x += 8) {
            int r, c, k;
            const sdk_tile_t *t = tile_at(x, y, &r, &c, &k);
            if (t->used && sdk_tile_colors(t->fmt) > n)
                n = sdk_tile_colors(t->fmt);
        }
    return n ? n : sdk_tile_colors(SDK_TILE_SNES4);
}

static void keep_colour(void)
{
    const int n = sdk_canvas_colours();
    if (cv.colour >= n)
        cv.colour = n - 1;
    if (cv.colour < 1)
        cv.colour = 1;
}

void sdk_canvas_format(int fmt)
{
    project_fmt = fmt;
    keep_colour();
}

void sdk_canvas_open(int r, int c, int size)
{
    if (size == 8 || size == 16)
        cv.size = size;
    const int tiles = cv.size / 8;
    cv.row = r < 0 ? 0 : r > SDK_SHEET_H - tiles ? SDK_SHEET_H - tiles : r;
    cv.col = c < 0 ? 0 : c > SDK_SHEET_W - tiles ? SDK_SHEET_W - tiles : c;
    cv.open = 1;
    cv.sel = 0;
    if (cv.x >= cv.size)
        cv.x = cv.size - 1;
    if (cv.y >= cv.size)
        cv.y = cv.size - 1;
    keep_colour();
}

int sdk_canvas_pixel(int x, int y)
{
    if (!cv.open || x < 0 || y < 0 || x >= cv.size || y >= cv.size)
        return 0;
    int r, c, k;
    const sdk_tile_t *t = tile_at(x, y, &r, &c, &k);
    return t->used ? t->pix[k] : 0;
}

/* Puts a colour number in a pixel. Returns 1 when it changed. A tile with nothing but 0 is an empty cell. */
static int put(int x, int y, int colour)
{
    if (!cv.open || x < 0 || y < 0 || x >= cv.size || y >= cv.size || sdk_canvas_pixel(x, y) == colour)
        return 0;
    int r, c, k;
    sdk_tile_t t = *tile_at(x, y, &r, &c, &k);
    if (!t.used) {
        memset(&t, 0, sizeof t);
        t.fmt = (uint8_t)(project_fmt >= 0 ? project_fmt : SDK_TILE_SNES4);
    }
    if (project_fmt < 0)                    /* the format grows to hold the colour */
        while (colour >= sdk_tile_colors(t.fmt) && t.fmt != SDK_TILE_SNES8)
            t.fmt = (uint8_t)(colour < 16 ? SDK_TILE_SNES4 : SDK_TILE_SNES8);
    t.pix[k] = (uint8_t)colour;
    t.used = 0;
    for (int i = 0; i < 64; i++)
        t.used |= t.pix[i] != 0;
    sdk_sheet_set(r, c, &t);
    return 1;
}

void sdk_canvas_colour_step(int step)
{
    const int n = sdk_canvas_colours() - 1;
    keep_colour();
    cv.colour = ((cv.colour - 1 + step) % n + n) % n + 1;
}

void sdk_canvas_move(int dx, int dy, int shift)
{
    if (!shift)
        cv.sel = 0;
    else if (!cv.sel) {
        cv.sel = 1;
        cv.ax = cv.x;
        cv.ay = cv.y;
    }
    cv.x += dx;
    cv.y += dy;
    cv.x = cv.x < 0 ? 0 : cv.x >= cv.size ? cv.size - 1 : cv.x;
    cv.y = cv.y < 0 ? 0 : cv.y >= cv.size ? cv.size - 1 : cv.y;
}

void sdk_canvas_select_all(void)
{
    cv.sel = 1;
    cv.ax = cv.ay = 0;
    cv.x = cv.y = cv.size - 1;
}

int sdk_canvas_rect(int *x1, int *y1, int *x2, int *y2)
{
    const int ax = cv.sel ? cv.ax : cv.x, ay = cv.sel ? cv.ay : cv.y;
    *x1 = ax < cv.x ? ax : cv.x;
    *x2 = ax < cv.x ? cv.x : ax;
    *y1 = ay < cv.y ? ay : cv.y;
    *y2 = ay < cv.y ? cv.y : ay;
    return cv.sel;
}

static int fill(int x1, int y1, int x2, int y2, int colour)
{
    int n = 0;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            n += put(x, y, colour);
    return n;
}

int sdk_canvas_paint(int colour, int cursor_only)
{
    int x1, y1, x2, y2;
    sdk_canvas_rect(&x1, &y1, &x2, &y2);
    if (cursor_only) {
        x1 = x2 = cv.x;
        y1 = y2 = cv.y;
    }
    return fill(x1, y1, x2, y2, colour);
}

int sdk_canvas_copy(void)
{
    if (!cv.open)
        return 0;
    int x1, y1, x2, y2;
    if (!sdk_canvas_rect(&x1, &y1, &x2, &y2)) {
        clip_w = clip_h = 0;
        sdk_sheet_copy(cv.row, cv.col, cv.row + cv.size / 8 - 1, cv.col + cv.size / 8 - 1);
        return 0;
    }
    clip_w = x2 - x1 + 1;
    clip_h = y2 - y1 + 1;
    for (int y = 0; y < clip_h; y++)
        for (int x = 0; x < clip_w; x++)
            clip[y * clip_w + x] = (uint8_t)sdk_canvas_pixel(x1 + x, y1 + y);
    clip_when = sdk_clip_stamp();
    return clip_w * clip_h;
}

int sdk_canvas_erase_copied(void)
{
    int x1, y1, x2, y2;
    if (!sdk_canvas_rect(&x1, &y1, &x2, &y2))
        return fill(0, 0, cv.size - 1, cv.size - 1, 0);
    return fill(x1, y1, x2, y2, 0);
}

int sdk_canvas_has_pixels(void) { return clip_w > 0 && clip_when == sdk_clip_stamp(); }

int sdk_canvas_paste(int *tiles)
{
    *tiles = 0;
    if (!cv.open)
        return -1;
    int n = 0;
    if (sdk_canvas_has_pixels()) {
        int x1, y1, x2, y2;
        sdk_canvas_rect(&x1, &y1, &x2, &y2);
        for (int y = 0; y < clip_h; y++)
            for (int x = 0; x < clip_w; x++)
                if (x1 + x < cv.size && y1 + y < cv.size) {
                    put(x1 + x, y1 + y, clip[y * clip_w + x]);
                    n++;
                }
        cv.x = x1;
        cv.y = y1;
        cv.sel = 0;
        return n;
    }
    int w, h;
    if (!sdk_clip_size(&w, &h))
        return -1;
    *tiles = 1;
    for (int r = 0; r < cv.size / 8 && r < h; r++)
        for (int c = 0; c < cv.size / 8 && c < w; c++) {
            sdk_sheet_set(cv.row + r, cv.col + c, sdk_clip_cell(r, c));
            n++;
        }
    keep_colour();
    return n;
}
