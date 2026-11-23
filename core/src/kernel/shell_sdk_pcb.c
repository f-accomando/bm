/*
 * Shell SDK, Project tab: the pictures (see shell_sdk_pcb.h). Original code.
 */
#include "shell_sdk_pcb.h"

#include <stdio.h>
#include <string.h>

#include "drivers/fb.h"
#include "gfx/console.h"
#include "gfx/draw.h"

typedef struct {
    framebuffer_t *fb;
    int cw, ch, top;        /* a cell in pixels, the y of ANSI row 1 */
} geo_t;

static int geo(geo_t *g)
{
    uint32_t w, h, top;
    g->fb = console_framebuffer();
    if (!g->fb)
        return 0;
    console_cell_geometry(&w, &h, &top);
    g->cw = (int)w;
    g->ch = (int)h;
    g->top = (int)top;
    return 1;
}

static int px(const geo_t *g, int col) { return (col - 1) * g->cw; }
static int py(const geo_t *g, int row) { return g->top + (row - 1) * g->ch; }

static uint32_t rgb(const geo_t *g, int r, int gr, int b) { return fb_color(g->fb, (uint8_t)r, (uint8_t)gr, (uint8_t)b); }

static void at(int row, int col)
{
    char b[40];
    snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
    console_write(b);
}

/* A filled circle (the coin cell). */
static void disc(const geo_t *g, int cx, int cy, int r, uint32_t color)
{
    for (int y = -r; y <= r; y++) {
        int half = 0;
        while ((half + 1) * (half + 1) + y * y <= r * r)
            half++;
        gfx_rect(g->fb, cx - half, cy + y, 2 * half + 1, 1, color);
    }
}

/* A chip of w x h cells: black cells with the label on the first row, pins above and below. */
static void chip(const geo_t *g, int col, int row, int w, int h, const char *label, const char *style)
{
    const uint32_t pin = rgb(g, 190, 190, 200);
    const int pw = g->cw / 3 > 1 ? g->cw / 3 : 1;
    const int ph = g->ch / 3 > 2 ? g->ch / 3 : 2;
    for (int x = px(g, col) + g->cw / 4; x + pw <= px(g, col + w); x += g->cw / 2) {
        gfx_rect(g->fb, x, py(g, row) - ph, pw, ph, pin);
        gfx_rect(g->fb, x, py(g, row + h), pw, ph, pin);
    }
    char line[40];
    const int n = (int)strlen(label);
    for (int i = 0; i < h; i++) {
        at(row + i, col);
        console_write(style);
        int len = 0;
        for (int c = 0; c < w && len < (int)sizeof line - 1; c++) {
            const int k = c - (w - n) / 2;
            line[len++] = i == 0 && k >= 0 && k < n ? label[k] : ' ';
        }
        line[len] = 0;
        console_write(line);
        console_write("\x1b[0m");
    }
}

#define CHIP_STYLE "\x1b[97;40m"

/* The connector: gold fingers along the bottom row of the box. */
static void fingers(const geo_t *g, int col, int row, int w)
{
    const uint32_t base = rgb(g, 60, 50, 20), gold = rgb(g, 230, 190, 60);
    const int x0 = px(g, col), y = py(g, row), n = w * g->cw;
    gfx_rect(g->fb, x0, y, n, g->ch, base);
    const int fw = g->cw / 3 > 1 ? g->cw / 3 : 1;
    for (int x = x0 + fw; x + fw <= x0 + n - fw; x += 2 * fw)
        gfx_rect(g->fb, x, y, fw, g->ch - 1, gold);
}

static void board(const geo_t *g, int col, int row, int w, int h)
{
    const uint32_t edge = rgb(g, 0, 70, 30), green = rgb(g, 0, 120, 55);
    const int x = px(g, col), y = py(g, row), pw = w * g->cw, ph = (h - 1) * g->ch;
    gfx_rect(g->fb, x, y, pw, ph, edge);
    gfx_rect(g->fb, x + 2, y + 2, pw - 4, ph - 2, green);
    fingers(g, col + 1, row + h - 1, w - 2);
}

static void battery(const geo_t *g, int col, int row, int w, int h)
{
    const int cx = px(g, col) + w * g->cw / 2, cy = py(g, row) + h * g->ch / 2;
    int r = h * g->ch / 2 - 1;
    if (r > w * g->cw / 2 - 1)
        r = w * g->cw / 2 - 1;
    disc(g, cx, cy, r, rgb(g, 120, 120, 130));
    disc(g, cx, cy, r - 2, rgb(g, 200, 200, 210));
    disc(g, cx, cy, r / 3, rgb(g, 150, 150, 160));
}

/* The footprint of a chip the board does not carry: an outline in the dark green. */
static void footprint(const geo_t *g, int col, int row, int w, int h)
{
    const uint32_t c = rgb(g, 0, 85, 40);
    const int x = px(g, col), y = py(g, row), pw = w * g->cw, ph = h * g->ch;
    gfx_rect(g->fb, x, y, pw, 1, c);
    gfx_rect(g->fb, x, y + ph - 1, pw, 1, c);
    gfx_rect(g->fb, x, y, 1, ph, c);
    gfx_rect(g->fb, x + pw - 1, y, 1, ph, c);
}

static void sd_card(const geo_t *g, int col, int row, int w, int h)
{
    const uint32_t body = rgb(g, 30, 50, 120), light = rgb(g, 60, 90, 170), gold = rgb(g, 230, 190, 60);
    const int x = px(g, col), y = py(g, row), pw = w * g->cw, ph = h * g->ch, cut = g->cw * 2;
    gfx_rect(g->fb, x, y, pw, ph, body);
    gfx_rect(g->fb, x + 2, y + 2, pw - 4, ph - 4, light);
    for (int k = 0; k < cut; k++)                       /* the cut corner, top right */
        gfx_rect(g->fb, x + pw - (cut - k), y + k, cut - k, 1, rgb(g, 0, 0, 0));
    for (int i = 0; i < 8; i++)
        gfx_rect(g->fb, x + g->cw + i * (pw - 2 * g->cw - cut) / 8, y + 2, g->cw / 2, g->ch, gold);
}

void sdk_pcb_draw(int target, const sdk_pcb_t *pcb, int col, int row, int w, int h)
{
    geo_t g;
    if (!geo(&g) || w < 30 || h < 9)
        return;
    if (target == SDK_BM) {
        const int cw = 20, ch = h - 1;
        const int c0 = col + (w - cw) / 2;
        sd_card(&g, c0, row, cw, ch);
        at(row + ch / 2 + 1, c0 + 4);
        console_write("\x1b[30;107m  bm project  \x1b[0m");
        at(row + ch / 2 + 2, c0 + 4);
        console_write("\x1b[30;107m   NAME.bm    \x1b[0m");
        return;
    }
    board(&g, col, row, w, h);
    const int hw = (w - 4) / 2;
    chip(&g, col + 2, row + 1, hw, 2, pcb->rom, CHIP_STYLE);
    if (target == SDK_NES) {
        chip(&g, col + w - 2 - hw, row + 1, hw, 2, pcb->rom2,
             pcb->chr_ram ? "\x1b[97;44m" : CHIP_STYLE);
    } else if (pcb->has_mapper) {
        chip(&g, col + w - 2 - hw, row + 1, hw, 2, pcb->mapper, "\x1b[30;103m");
    } else {
        footprint(&g, col + w - 2 - hw, row + 1, hw, 2);
    }
    const int low = row + 5;
    if (pcb->has_ram)
        chip(&g, col + 2, low, 9, 2, "SRAM", "\x1b[97;100m");
    else
        footprint(&g, col + 2, low, 9, 2);
    if (target == SDK_NES) {
        if (pcb->has_mapper)
            chip(&g, col + 13, low, 9, 2, pcb->mapper, "\x1b[30;103m");
        else
            footprint(&g, col + 13, low, 9, 2);
    } else {
        chip(&g, col + 13, low, 6, 2, "CIC", CHIP_STYLE);
    }
    if (pcb->has_battery)
        battery(&g, col + w - 9, low - 1, 6, 4);
}

/* A cartridge or a card in a box of cells, in pixels. */
static void cart_art(const geo_t *g, int target, int col, int row, int w, int h)
{
    const int x = px(g, col), y = py(g, row), pw = w * g->cw, ph = h * g->ch;
    if (target == SDK_NES) {
        const uint32_t body = rgb(g, 150, 150, 150), dark = rgb(g, 90, 90, 90), lab = rgb(g, 200, 60, 50);
        gfx_rect(g->fb, x, y, pw, ph, body);
        gfx_rect(g->fb, x, y, pw, 2, dark);
        gfx_rect(g->fb, x + pw / 6, y + ph / 5, pw * 2 / 3, ph / 2, lab);       /* the label */
        gfx_rect(g->fb, x + pw / 8, y + ph - ph / 5, pw / 4, 2, dark);           /* the ridges at the bottom */
        gfx_rect(g->fb, x + pw - pw / 8 - pw / 4, y + ph - ph / 5, pw / 4, 2, dark);
    } else if (target == SDK_SNES) {
        const uint32_t body = rgb(g, 200, 200, 205), dark = rgb(g, 120, 120, 130), lab = rgb(g, 90, 80, 150);
        gfx_rect(g->fb, x, y, pw, ph - ph / 6, body);
        gfx_rect(g->fb, x + pw / 6, y + ph - ph / 6, pw * 2 / 3, ph / 6, body);  /* the lip with the edge connector */
        gfx_rect(g->fb, x + pw / 8, y + ph / 6, pw * 3 / 4, ph / 2, lab);
        gfx_rect(g->fb, x + pw / 8, y + 2, pw * 3 / 4, 2, dark);
        gfx_rect(g->fb, x + pw / 8, y + ph / 6 + ph / 2 + 3, pw * 3 / 4, 2, dark);
    } else {
        sd_card(g, col, row, w, h);
    }
}

void sdk_tile_draw(int target, int selected, int col, int row, int w, int h)
{
    geo_t g;
    if (!geo(&g) || w < 13 || h < 4)
        return;
    if (selected)                                       /* a dark yellow ground under the whole tile */
        gfx_rect(g.fb, px(&g, col), py(&g, row), w * g.cw, h * g.ch, rgb(&g, 90, 70, 0));
    cart_art(&g, target, col + 1, row, 4, h - 1);
    chip(&g, col + 6, row + 1, 6, 1, sdk_target_cpu(target), CHIP_STYLE);
    char name[16];
    snprintf(name, sizeof name, "%s", target == SDK_BM ? "bm project" : target == SDK_NES ? "NES cart" : "SNES cart");
    at(row + h - 1, col + 1);
    console_write(selected ? "\x1b[93m" : "\x1b[97m");
    console_write(name);
    console_write("\x1b[0m");
}
