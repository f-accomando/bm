/*
 * Shell SDK: the tiles on the screen (see shell_sdk_sprite.h). Original code.
 */
#include "shell_sdk_sprite.h"

#include <stdio.h>
#include <string.h>

#include "drivers/fb.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "shell_sdk_canvas.h"
#include "shell_sdk_chr.h"
#include "shell_sdk_code.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_keys.h"
#include "shell_sdk_pal.h"
#include "shell_sdk_pane.h"
#include "shell_sdk_sheet.h"
#include "shell_sdk_tiles.h"
#include "usb/hid.h"

#define TILES_GUTTER  7                 /* "004010 ": the offset of a row, as in the dump */
#define SHEET_GUTTER  4                 /* "010 ": the number of the first tile of a row */
#define FIRST         3                 /* the ANSI row of the first row of tiles of the ROM */
#define SHEET_FIRST   4                 /* and of the sheet and of the canvas, under two lines */
#define CODE_ROWS     6                 /* the code under the panes: its head and five lines */

enum { ON_LEFT, ON_SHEET };

/* the ROM and its view */
static const uint8_t *rom;
static size_t rom_size;
static sdk_rom_t rom_info;
static char rom_name[48];
static size_t view_off;                 /* the offset of the first tile shown */
static int view_fmt, view_chr;          /* how the tiles are read; the view started at the CHR ROM */
static int view_per = SDK_TILES_ROW;    /* tiles in a row, chosen with + and - */
static int view_tall;                   /* the 8x16 order: a tile and the next above each other */
static sdk_grid_t cursor;               /* the cursor and the selection, in cells of the view */
static int view_col, view_rows = 1;     /* where the tiles are on the screen, set when the view is drawn */
static int view_shown = SDK_TILES_ROW, view_fit = SDK_TILES_ROW_MAX;    /* in a row: drawn, and at most */
static int view_scale = 1;              /* pixels of the screen for one of a tile */
static int view_active;                 /* the view has the keys, set when it is drawn */

/* the Sprite tab */
static int focus = ON_SHEET;
static int hidden;                      /* the ROM is open but the canvas has its place */
static sdk_grid_t pick;                 /* the cursor and the selection of the sheet */
static int sheet_top, sheet_rows = 1, sheet_scale = 1;
static int canvas_px = 8;               /* pixels of the screen for one of the canvas */
static int code_kind;                   /* SDK_SPRITE_*: the code the sheet is written in */
static unsigned code_rev;               /* the revision of the code the sheet was read from */
static char code_err[48];               /* why its tiles block cannot be read ("": it can) */
static char info[2][48];                /* the head of the sheet: its size, for this project */
static int import_armed;                /* i was pressed on a sheet with tiles: again replaces them */
static int show_code = 1;               /* the code under the panes (v) */

void sdk_sprite_reset(void)
{
    sdk_sheet_reset();
    sdk_canvas_reset();
    sdk_pal_reset();
    memset(&pick, 0, sizeof pick);
    sheet_top = 0;
    focus = ON_SHEET;
    hidden = 0;
    code_kind = SDK_SPRITE_NONE;
    code_rev = 0;
    code_err[0] = 0;
    info[0][0] = info[1][0] = 0;
    import_armed = 0;
    show_code = 1;
}

static int chr_kind(void)
{
    return code_kind == SDK_SPRITE_NES_C ? SDK_CHR_NES_C : code_kind == SDK_SPRITE_NES ? SDK_CHR_NES :
           code_kind == SDK_SPRITE_SNES_C ? SDK_CHR_SNES_C : SDK_CHR_SNES;
}

/* The sheet and the palettes as the code has them. */
static void read_code(void)
{
    code_rev = sdk_edit_revision();
    code_err[0] = 0;
    if (code_kind != SDK_SPRITE_NONE &&
        sdk_chr_read(chr_kind(), sdk_edit_text(), sdk_edit_length(), code_err, sizeof code_err))
        sdk_sheet_clear(0, 0, SDK_SHEET_H - 1, SDK_SHEET_W - 1);
    sdk_pal_load(code_kind, sdk_edit_text(), sdk_edit_length());
}

void sdk_sprite_sync(const sdk_proj_t *p)
{
    const int code = p->target == SDK_NES ? (p->cart_c ? SDK_SPRITE_NES_C : SDK_SPRITE_NES) :
                     p->target == SDK_SNES ? (p->cart_c ? SDK_SPRITE_SNES_C : SDK_SPRITE_SNES) : SDK_SPRITE_NONE;
    const int changed = code != code_kind || sdk_edit_revision() != code_rev;
    code_kind = code;
    sdk_proj_sheet(p, info);
    sdk_canvas_format(code == SDK_SPRITE_NONE ? -1 : sdk_chr_format(code == SDK_SPRITE_NES || code == SDK_SPRITE_NES_C));
    if (changed)
        read_code();
}

void sdk_sprite_rom(const uint8_t *data, size_t size, const sdk_rom_t *info, const char *name)
{
    rom = data;
    rom_size = data ? size : 0;
    hidden = 0;
    focus = data ? ON_LEFT : ON_SHEET;
    memset(&cursor, 0, sizeof cursor);
    view_per = SDK_TILES_ROW;
    view_tall = 0;
    if (!data)
        return;
    rom_info = *info;
    snprintf(rom_name, sizeof rom_name, "%s", name);
    view_off = sdk_tiles_start(rom, rom_size, &rom_info, &view_fmt, &view_chr);
}

/* ---- pixels ---- */

/* The colours of a game are not in its tiles: the colour numbers show as greys (or with the palette chosen in
 * the Sprite tab, pal), 0 as dark blue. */
static void paint_tile(framebuffer_t *fb, const uint8_t pix[64], int fmt, int x0, int y0, int scale, int pal)
{
    const int n = sdk_tile_colors(fmt);
    for (int y = 0; y < 8; y++)
        for (int x = 0, e; x < 8; x = e) {              /* a run of one colour is one rectangle */
            const int v = pix[y * 8 + x];
            for (e = x + 1; e < 8 && pix[y * 8 + e] == v; e++)
                ;
            const uint32_t g = (uint32_t)(40 + 215 * v / (n - 1));
            const uint32_t rgb = !v ? 56 : pal ? sdk_pal_rgb(v, n) : g << 16 | g << 8 | g;
            gfx_rect(fb, x0 + x * scale, y0 + y * scale, (e - x) * scale, scale,
                     fb_color(fb, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb));
        }
}

/* The frames of a grid whose first cell is at (x0, y0), rows rows high: the selection in cyan, the cursor in
 * yellow when the pane has the keys. The cells are from the row `from` of the grid. */
static void paint_cursor(framebuffer_t *fb, const sdk_grid_t *g, int x0, int y0, int side, int from, int rows,
                         int active)
{
    const int t = side > 8 ? 2 : 1;
    int r1, c1, r2, c2;
    for (int k = 0; k < 2; k++) {
        if (k == 0) {
            if (!sdk_grid_rect(g, &r1, &c1, &r2, &c2))
                continue;
        } else {
            if (!active)
                continue;
            r1 = r2 = g->row;
            c1 = c2 = g->col;
        }
        r1 -= from;
        r2 -= from;
        if (r2 < 0 || r1 >= rows)
            continue;
        if (r1 < 0)
            r1 = 0;
        if (r2 >= rows)
            r2 = rows - 1;
        const uint32_t color = k ? fb_color(fb, 255, 220, 0) : fb_color(fb, 0, 200, 255);
        const int x = x0 + c1 * side, y = y0 + r1 * side;
        const int w = (c2 - c1 + 1) * side, h = (r2 - r1 + 1) * side;
        gfx_rect(fb, x, y, w, t, color);
        gfx_rect(fb, x, y + h - t, w, t, color);
        gfx_rect(fb, x, y, t, h, color);
        gfx_rect(fb, x + w - t, y, t, h, color);
    }
}

/* ---- the view of the ROM ---- */

/* The size of the grid for a pane: the tiles are as high as a row of text, smaller when more of them are asked
 * in a row than the pane holds at that size. */
static void view_layout(int width, int rows)
{
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int pixels = (width - TILES_GUTTER) * (int)cw;
    view_fit = pixels / 8;
    if (view_fit > SDK_TILES_ROW_MAX)
        view_fit = SDK_TILES_ROW_MAX;
    if (view_fit < 1)
        view_fit = 1;
    view_shown = view_per < view_fit ? view_per : view_fit;
    view_scale = (int)ch / 8;
    const int widest = pixels / (view_shown * 8);
    if (view_scale > widest)
        view_scale = widest;
    if (view_scale < 1)
        view_scale = 1;
    const int text = rows - FIRST > 0 ? rows - FIRST : 1;       /* the rows above the last one */
    view_rows = text * (int)ch / (8 * view_scale);
    if (view_tall)
        view_rows &= ~1;                                        /* whole sprites of two tiles */
    if (view_rows < (view_tall ? 2 : 1))
        view_rows = view_tall ? 2 : 1;
    if (cursor.row >= view_rows)
        cursor.row = view_rows - 1;
    if (cursor.col >= view_shown)
        cursor.col = view_shown - 1;
}

/* The offset of the tile of a cell of the view, or -1 outside the ROM. A selection can start above the view. */
static long cell_offset(int r, int c)
{
    const long off = (long)view_off + (long)sdk_tiles_cell(r, c, view_shown, view_tall) *
                     (long)sdk_tile_bytes(view_fmt);
    return off >= 0 && off < (long)rom_size ? off : -1;
}

void sdk_tiles_draw(int col, int width, int rows, int code, int active)
{
    view_col = col;
    view_active = active;
    view_layout(width, rows);
    char b[64];
    snprintf(b, sizeof b, "%s  %s  %d/row %s",
             rom_info.kind != SDK_ROM_NES ? "ROM" : view_chr ? "CHR ROM" : "PRG (CHR RAM)",
             sdk_tile_name(view_fmt), view_shown, view_tall ? "8x16" : "8x8");
    console_write("\x1b[93m");
    sdk_put_row(2, col, width, b, (int)strlen(b));
    console_write("\x1b[90m");
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int pitch = 8 * view_scale;
    for (int i = 0; i < rows - FIRST; i++) {            /* the offset of the first row of tiles of a text row */
        const int r = (i * (int)ch + pitch - 1) / pitch;
        if (r >= view_rows || r * pitch >= (i + 1) * (int)ch)
            continue;
        const long off = cell_offset(r, 0);
        if (off >= 0) {
            snprintf(b, sizeof b, "%06X", (unsigned)off);
            sdk_put_row(FIRST + i, col, TILES_GUTTER, b, (int)strlen(b));
        }
    }
    const long off = cell_offset(cursor.row, cursor.col);
    snprintf(b, sizeof b, "Offset %06X", (unsigned)(off < 0 ? 0 : off));
    (void)code;
    sdk_pane_status(col, width, rows, b, active);
}

void sdk_tiles_paint(void)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb || !rom)
        return;
    uint32_t cw, ch, top;
    console_cell_geometry(&cw, &ch, &top);
    const int side = 8 * view_scale;
    const int x0 = (view_col - 1 + TILES_GUTTER) * (int)cw;
    const int y0 = (int)top + (FIRST - 1) * (int)ch;
    for (int r = 0; r < view_rows; r++)
        for (int c = 0; c < view_shown; c++) {
            const long off = cell_offset(r, c);
            if (off < 0)
                continue;
            uint8_t pix[64];
            sdk_tile_decode(rom, rom_size, (size_t)off, view_fmt, pix);
            paint_tile(fb, pix, view_fmt, x0 + c * side, y0 + r * side, view_scale, 0);
        }
    paint_cursor(fb, &cursor, x0, y0, side, 0, view_rows, view_active);
}

static void copy_from_rom(void)
{
    int r1, c1, r2, c2, w, h;
    sdk_grid_rect(&cursor, &r1, &c1, &r2, &c2);
    sdk_clip_start(c2 - c1 + 1, r2 - r1 + 1);
    sdk_clip_size(&w, &h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const long off = cell_offset(r1 + y, c1 + x);
            if (off < 0)
                continue;
            sdk_tile_t *t = sdk_clip_cell(y, x);
            t->used = 1;
            t->fmt = (uint8_t)view_fmt;
            sdk_tile_decode(rom, rom_size, (size_t)off, view_fmt, t->pix);
        }
    if (h < r2 - r1 + 1)
        snprintf(sdk_note, sizeof sdk_note, "Copied %dx%d tiles, the most it holds", w, h);
    else
        snprintf(sdk_note, sizeof sdk_note, "Copied %dx%d tiles", w, h);
}

int sdk_tiles_key(int key, int mods)
{
    if (!rom)
        return 0;
    const int ctrl = (mods & SDK_MOD_CTRL) != 0, shift = (mods & SDK_MOD_SHIFT) != 0;
    int dr = 0, dc = 0, move = -1;
    if (key == HID_KEY_UP)
        dr = -1;
    else if (key == HID_KEY_DOWN)
        dr = 1;
    else if (key == HID_KEY_LEFT && ctrl)
        move = SDK_TILES_BACK;
    else if (key == HID_KEY_RIGHT && ctrl)
        move = SDK_TILES_NEXT;
    else if (key == HID_KEY_LEFT)
        dc = -1;
    else if (key == HID_KEY_RIGHT)
        dc = 1;
    else if (key == ',')
        move = SDK_TILES_LEFT;
    else if (key == '.')
        move = SDK_TILES_RIGHT;
    else if (key == HID_KEY_PGUP)
        move = SDK_TILES_PGUP;
    else if (key == HID_KEY_PGDN)
        move = SDK_TILES_PGDN;
    else if (key == HID_KEY_END)
        move = SDK_TILES_END;
    if (dr || dc) {
        sdk_tiles_cursor(&cursor, &view_off, rom_size, view_fmt, view_shown, view_tall, view_rows, dr, dc, shift);
        return 1;
    }
    if (move >= 0) {                            /* the tiles under the cells change: no selection */
        const int high = view_tall ? 2 : 1;
        view_off = sdk_tiles_move(view_off, rom_size, view_fmt, view_shown * high, view_rows / high, move);
        cursor.sel = 0;
        return 1;
    }
    if (key == HID_KEY_HOME) {                  /* back where the view started */
        int fmt;
        view_off = sdk_tiles_start(rom, rom_size, &rom_info, &fmt, &view_chr);
        memset(&cursor, 0, sizeof cursor);
        return 1;
    }
    if (key == '+' || key == '=' || key == '-') {          /* tiles in a row: one more, one less */
        const int per = view_shown + (key == '-' ? -1 : 1);
        if (per < 1 || per > view_fit)
            snprintf(sdk_note, sizeof sdk_note, per < 1 ? "One tile a row at least" :
                                                          "No room for more tiles in a row");
        else {
            view_per = view_shown = per;
            cursor.sel = 0;
            if (cursor.col >= per)
                cursor.col = per - 1;
        }
        return 1;
    }
    if (key == 'o') {                           /* the order: 8x8, or 8x16 sprites */
        view_tall = !view_tall;
        cursor.sel = 0;
        return 1;
    }
    if (key == 'b') {
        if (sdk_tile_next(view_fmt) == view_fmt)
            snprintf(sdk_note, sizeof sdk_note, "NES tiles are 2bpp");
        view_fmt = sdk_tile_next(view_fmt);
        cursor.sel = 0;
        return 1;
    }
    if (key == 0x01) {                          /* Ctrl+A: all that shows */
        cursor.sel = 1;
        cursor.arow = cursor.acol = 0;
        cursor.row = view_rows - 1;
        cursor.col = view_shown - 1;
        return 1;
    }
    if (key == 0x03) {                          /* Ctrl+C */
        copy_from_rom();
        return 1;
    }
    if (key == 0x18 || key == 0x16) {           /* Ctrl+X, Ctrl+V */
        snprintf(sdk_note, sizeof sdk_note, "The ROM is read only: Ctrl+C copies");
        return 1;
    }
    return 0;
}

/* ---- changes of the sheet ---- */

/* A change of the sheet can go into the code: with a block that cannot be read it would be lost. */
static int can_change(void)
{
    if (!code_err[0])
        return 1;
    snprintf(sdk_note, sizeof sdk_note, "Fix the tiles block in Code first");
    return 0;
}

/* The sheet changed: into the code, and back from it, so that it shows what the code holds. run as in
 * sdk_chr_commit. */
static void changed_sheet(int run)
{
    if (code_kind == SDK_SPRITE_NONE)
        return;
    char err[48];
    if (sdk_chr_commit(chr_kind(), run, err, sizeof err) != 0)
        snprintf(sdk_note, sizeof sdk_note, "%s", err);
    read_code();
}

/* 1 when a tile of the clipboard has a colour number that the format of the code cannot hold. */
static int colours_lost(void)
{
    int w, h;
    if (code_kind == SDK_SPRITE_NONE || !sdk_clip_size(&w, &h))
        return 0;
    const int colours = sdk_tile_colors(sdk_chr_format(chr_kind() == SDK_CHR_NES || chr_kind() == SDK_CHR_NES_C));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const sdk_tile_t *t = sdk_clip_cell(y, x);
            for (int i = 0; t->used && i < 64; i++)
                if (t->pix[i] >= colours)
                    return 1;
        }
    return 0;
}

static void say_palette(void)
{
    if (sdk_pal_count() > 1)
        snprintf(sdk_note, sizeof sdk_note, "Palette: %s", sdk_pal_name());
    else
        snprintf(sdk_note, sizeof sdk_note, code_kind == SDK_SPRITE_NES_C || code_kind == SDK_SPRITE_SNES_C ?
                 "No palette: call sprite_palette()" : "No palette: add the label palette");
}

/* The keys of the pen, on the canvas and on the sheet. Returns 1 when it used the key. */
static int pen_key(int key)
{
    if (key == 's') {
        const int size = sdk_canvas()->size == 8 ? 16 : 8;
        sdk_canvas_size(size);
        snprintf(sdk_note, sizeof sdk_note, "Canvas %dx%d", size, size);
    } else if (key == 'p') {
        sdk_pal_next();
        say_palette();
    } else if (key == '[' || key == ']') {
        sdk_canvas_colour_step(key == '[' ? -1 : 1);
        snprintf(sdk_note, sizeof sdk_note, "Colour %d of %d", sdk_canvas()->colour, sdk_canvas_colours() - 1);
    } else
        return 0;
    return 1;
}

/* ---- the canvas ---- */

/* Paints the selection or the pixel of the cursor (sdk_canvas_paint). The pixels of one block are one step of
 * the undo of the code. */
static void paint(int colour, int cursor_only)
{
    if (can_change() && sdk_canvas_paint(colour, cursor_only))
        changed_sheet(sdk_canvas()->row * SDK_SHEET_W + sdk_canvas()->col + 1);
}

static void text_row(int row, int col, int width, const char *color, const char *s)
{
    console_write(color);
    sdk_put_row(row, col, width, s, (int)strlen(s));
    console_write("\x1b[0m");
}

static void canvas_draw(int col, int width, int rows)
{
    const sdk_canvas_t *cv = sdk_canvas();
    const int active = focus == ON_LEFT;
    char b[80];
    if (!cv->open)
        snprintf(b, sizeof b, "Canvas %dx%d", cv->size, cv->size);
    else
        snprintf(b, sizeof b, "Canvas %dx%d  tile %03X", cv->size, cv->size,
                 (unsigned)(cv->row * SDK_SHEET_W + cv->col));
    sdk_title_bar(1, col, width, b, active);
    if (!cv->open) {
        text_row(FIRST, col + 1, width - 1, "\x1b[90m", "No tile open");
        sdk_pane_status(col, width, rows, "", active);
        return;
    }
    snprintf(b, sizeof b, " Colour %d/%d  palette: %s", cv->colour, sdk_canvas_colours() - 1, sdk_pal_name());
    text_row(2, col, width, "\x1b[93m", b);
    snprintf(b, sizeof b, " Pixel %d,%d", cv->x, cv->y);
    text_row(3, col, width, "\x1b[90m", b);
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int text = rows - SHEET_FIRST - 1 > 0 ? rows - SHEET_FIRST - 1 : 1;     /* above the colours */
    canvas_px = text * (int)ch / cv->size;
    if (canvas_px > (width - 2) * (int)cw / cv->size)
        canvas_px = (width - 2) * (int)cw / cv->size;
    if (canvas_px < 2)
        canvas_px = 2;
    sdk_pane_status(col, width, rows, "", active);
}

/* A transparent pixel: two dark greys in a chequer. */
static void paint_clear(framebuffer_t *fb, int x, int y, int w, int h)
{
    const uint32_t a = fb_color(fb, 24, 24, 24), b = fb_color(fb, 48, 48, 48);
    gfx_rect(fb, x, y, w, h, a);
    gfx_rect(fb, x + w / 2, y, w - w / 2, h / 2, b);
    gfx_rect(fb, x, y + h / 2, w / 2, h - h / 2, b);
}

static uint32_t rgb_color(framebuffer_t *fb, uint32_t rgb)
{
    return fb_color(fb, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

static void canvas_paint(int col, int width, int rows)
{
    framebuffer_t *fb = console_framebuffer();
    const sdk_canvas_t *cv = sdk_canvas();
    if (!fb || !cv->open)
        return;
    uint32_t cw, ch, top;
    console_cell_geometry(&cw, &ch, &top);
    const int n = sdk_canvas_colours(), px = canvas_px, side = cv->size * px;
    const int x0 = col * (int)cw;                                   /* one cell of margin */
    const int y0 = (int)top + (SHEET_FIRST - 1) * (int)ch;
    gfx_rect(fb, x0, y0, side + 1, side + 1, fb_color(fb, 80, 80, 80));
    for (int y = 0; y < cv->size; y++)
        for (int x = 0; x < cv->size; x++) {
            const int v = sdk_canvas_pixel(x, y);
            const int left = x0 + x * px + 1, high = y0 + y * px + 1;
            if (v)
                gfx_rect(fb, left, high, px - 1, px - 1, rgb_color(fb, sdk_pal_rgb(v, n)));
            else
                paint_clear(fb, left, high, px - 1, px - 1);
        }
    if (cv->size == 16) {                                           /* where the four tiles meet */
        gfx_rect(fb, x0 + 8 * px, y0, 1, side, fb_color(fb, 170, 170, 170));
        gfx_rect(fb, x0, y0 + 8 * px, side, 1, fb_color(fb, 170, 170, 170));
    }
    const sdk_grid_t g = { .row = cv->y, .col = cv->x, .sel = cv->sel, .arow = cv->ay, .acol = cv->ax };
    paint_cursor(fb, &g, x0, y0, px, 0, cv->size, focus == ON_LEFT);

    /* the colours: 0 is the transparent one, the pen has a frame; of 256, the 16 around the pen */
    const int count = n > 16 ? 16 : n, first = n > 16 ? cv->colour / 16 * 16 : 0;
    int sw = (width - 2) * (int)cw / count;
    if (sw > (int)ch)
        sw = (int)ch;
    const int sy = (int)top + (rows - 2) * (int)ch + 3, sh = (int)ch - 4;     /* a gap under the grid */
    for (int i = 0; i < count; i++) {
        if (first + i == 0)
            paint_clear(fb, x0 + i * sw, sy, sw - 1, sh);
        else
            gfx_rect(fb, x0 + i * sw, sy, sw - 1, sh, rgb_color(fb, sdk_pal_rgb(first + i, n)));
    }
    const int at = x0 + (cv->colour - first) * sw;
    const uint32_t yellow = fb_color(fb, 255, 220, 0);
    gfx_rect(fb, at, sy, sw - 1, 2, yellow);
    gfx_rect(fb, at, sy + sh - 2, sw - 1, 2, yellow);
    gfx_rect(fb, at, sy, 2, sh, yellow);
    gfx_rect(fb, at + sw - 3, sy, 2, sh, yellow);
}

static int canvas_key(int key, int mods)
{
    const int shift = (mods & SDK_MOD_SHIFT) != 0;
    int dx = 0, dy = 0;
    switch (key) {
    case HID_KEY_UP: dy = -1; break;
    case HID_KEY_DOWN: dy = 1; break;
    case HID_KEY_LEFT: dx = -1; break;
    case HID_KEY_RIGHT: dx = 1; break;
    case HID_KEY_PGUP: dy = -16; break;
    case HID_KEY_PGDN: dy = 16; break;
    case HID_KEY_HOME: dx = -16; break;
    case HID_KEY_END: dx = 16; break;
    default: break;
    }
    if (dx || dy) {
        sdk_canvas_move(dx, dy, shift);
        if (!shift && (mods & SDK_MOD_PAINT))
            paint(sdk_canvas()->colour, 1);
        else if (!shift && (mods & SDK_MOD_ERASE))
            paint(0, 1);
        return 1;
    }
    int n, tiles;
    switch (key) {
    case ' ':
        paint(sdk_canvas()->colour, 0);
        return 1;
    case HID_KEY_DEL:
    case 0x08:
    case 0x7F:
        paint(0, 0);
        return 1;
    case '\r':
    case '\n':
        focus = ON_SHEET;
        return 1;
    case 0x01:                                  /* Ctrl+A */
        sdk_canvas_select_all();
        return 1;
    case 0x03:                                  /* Ctrl+C */
    case 0x18:                                  /* Ctrl+X */
        if (key == 0x18 && !can_change())
            return 1;
        n = sdk_canvas_copy();
        if (n)
            snprintf(sdk_note, sizeof sdk_note, "%s %d pixels", key == 0x18 ? "Cut" : "Copied", n);
        else
            snprintf(sdk_note, sizeof sdk_note, "%s the tiles of the canvas", key == 0x18 ? "Cut" : "Copied");
        if (key == 0x18 && sdk_canvas_erase_copied())
            changed_sheet(0);
        return 1;
    case 0x16: {                                /* Ctrl+V */
        if (!can_change())
            return 1;
        const int cut = colours_lost();
        n = sdk_canvas_paste(&tiles);
        if (n < 0)
            snprintf(sdk_note, sizeof sdk_note, "Nothing to paste: copy first");
        else {
            snprintf(sdk_note, sizeof sdk_note, "Pasted %d %s%s", n, tiles ? "tiles" : "pixels",
                     tiles && cut ? ", colours cut" : "");
            changed_sheet(0);
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* ---- the sprite sheet ---- */

static void sheet_draw(int col, int width, int rows, int active)
{
    sdk_title_bar(1, col, width, "Spritesheet", active);
    text_row(2, col, width, "\x1b[93m", info[0]);
    if (code_err[0]) {
        char b[80];
        snprintf(b, sizeof b, "Code: %s", code_err);
        text_row(3, col, width, "\x1b[91m", b);
    } else
        text_row(3, col, width, "\x1b[90m", info[1]);
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int text = rows - SHEET_FIRST > 0 ? rows - SHEET_FIRST : 1;
    sheet_scale = (int)ch / 8;
    if (sheet_scale > (width - SHEET_GUTTER) * (int)cw / (SDK_SHEET_W * 8))
        sheet_scale = (width - SHEET_GUTTER) * (int)cw / (SDK_SHEET_W * 8);
    if (sheet_scale < 1)
        sheet_scale = 1;
    const int side = 8 * sheet_scale;
    sheet_rows = text * (int)ch / side;
    if (sheet_rows > SDK_SHEET_H)
        sheet_rows = SDK_SHEET_H;
    if (sheet_rows < 1)
        sheet_rows = 1;
    if (pick.row < sheet_top)
        sheet_top = pick.row;
    if (pick.row >= sheet_top + sheet_rows)
        sheet_top = pick.row - sheet_rows + 1;
    if (sheet_top > SDK_SHEET_H - sheet_rows)
        sheet_top = SDK_SHEET_H - sheet_rows;
    console_write("\x1b[90m");
    for (int i = 0; i < text; i++) {
        const int r = (i * (int)ch + side - 1) / side;
        if (r >= sheet_rows || r * side >= (i + 1) * (int)ch)
            continue;
        char b[16];
        snprintf(b, sizeof b, "%03X", (unsigned)((sheet_top + r) * SDK_SHEET_W));
        sdk_put_row(SHEET_FIRST + i, col, SHEET_GUTTER, b, (int)strlen(b));
    }
    char hint[48];
    snprintf(hint, sizeof hint, "Tile %03X", (unsigned)(pick.row * SDK_SHEET_W + pick.col));
    sdk_pane_status(col, width, rows, hint, active);
}

static void sheet_paint(int col)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb)
        return;
    uint32_t cw, ch, top;
    console_cell_geometry(&cw, &ch, &top);
    const int side = 8 * sheet_scale;
    const int x0 = (col - 1 + SHEET_GUTTER) * (int)cw;
    const int y0 = (int)top + (SHEET_FIRST - 1) * (int)ch;
    for (int r = 0; r < sheet_rows; r++)
        for (int c = 0; c < SDK_SHEET_W; c++) {
            const sdk_tile_t *t = sdk_sheet_tile(sheet_top + r, c);
            const int x = x0 + c * side, y = y0 + r * side;
            if (t->used)
                paint_tile(fb, t->pix, t->fmt, x, y, sheet_scale, 1);
            else {                              /* an empty cell: its edges show the grid */
                gfx_rect(fb, x, y, side, side, fb_color(fb, 56, 56, 56));
                gfx_rect(fb, x + 1, y + 1, side - 1, side - 1, fb_color(fb, 16, 16, 16));
            }
        }
    const sdk_canvas_t *cv = sdk_canvas();
    if (cv->open) {                             /* the block in the canvas */
        const int tiles = cv->size / 8, from = cv->row - sheet_top;
        const int r1 = from < 0 ? 0 : from;
        const int r2 = from + tiles > sheet_rows ? sheet_rows : from + tiles;
        if (r1 < r2) {
            const int x = x0 + cv->col * side, y = y0 + r1 * side;
            const int w = tiles * side, h = (r2 - r1) * side;
            const uint32_t color = fb_color(fb, 255, 0, 200);
            gfx_rect(fb, x, y, w, 2, color);
            gfx_rect(fb, x, y + h - 2, w, 2, color);
            gfx_rect(fb, x, y, 2, h, color);
            gfx_rect(fb, x + w - 2, y, 2, h, color);
        }
    }
    paint_cursor(fb, &pick, x0, y0, side, sheet_top, sheet_rows, focus == ON_SHEET);
}

static int sheet_key(int key, int mods)
{
    const int shift = (mods & SDK_MOD_SHIFT) != 0;
    int dr = 0, dc = 0;
    switch (key) {
    case HID_KEY_UP: dr = -1; break;
    case HID_KEY_DOWN: dr = 1; break;
    case HID_KEY_LEFT: dc = -1; break;
    case HID_KEY_RIGHT: dc = 1; break;
    case HID_KEY_PGUP: dr = -sheet_rows; break;
    case HID_KEY_PGDN: dr = sheet_rows; break;
    case HID_KEY_HOME: dc = -SDK_SHEET_W; break;
    case HID_KEY_END: dc = SDK_SHEET_W; break;
    default: break;
    }
    if (dr || dc) {
        sdk_grid_move(&pick, dr, dc, shift, SDK_SHEET_H, SDK_SHEET_W);
        return 1;
    }
    int r1, c1, r2, c2, w, h;
    sdk_grid_rect(&pick, &r1, &c1, &r2, &c2);
    switch (key) {
    case '\r':
    case '\n':                                  /* the tile, or a 2x2 selection, into the canvas */
        if (pick.sel && r2 - r1 == 1 && c2 - c1 == 1)
            sdk_canvas_open(r1, c1, 16);
        else
            sdk_canvas_open(pick.row, pick.col, 0);
        pick.sel = 0;
        hidden = 1;
        focus = ON_LEFT;
        return 1;
    case 0x01:                                  /* Ctrl+A */
        pick.sel = 1;
        pick.arow = pick.acol = 0;
        pick.row = SDK_SHEET_H - 1;
        pick.col = SDK_SHEET_W - 1;
        return 1;
    case 0x03:                                  /* Ctrl+C */
    case 0x18:                                  /* Ctrl+X */
        if (key == 0x18 && !can_change())
            return 1;
        sdk_sheet_copy(r1, c1, r2, c2);
        sdk_clip_size(&w, &h);
        snprintf(sdk_note, sizeof sdk_note, "%s %dx%d tiles", key == 0x18 ? "Cut" : "Copied", w, h);
        if (key == 0x18) {
            sdk_sheet_clear(r1, c1, r2, c2);
            changed_sheet(0);
        }
        return 1;
    case 0x16: {                                /* Ctrl+V: at the first cell of the selection */
        if (sdk_canvas_has_pixels()) {
            snprintf(sdk_note, sizeof sdk_note, "Pixels paste in the canvas");
            return 1;
        }
        if (!sdk_clip_size(&w, &h)) {
            snprintf(sdk_note, sizeof sdk_note, "Nothing to paste: copy first");
            return 1;
        }
        if (!can_change())
            return 1;
        const int cut = colours_lost();
        const int n = sdk_sheet_paste(r1, c1);
        pick.row = r1;
        pick.col = c1;
        pick.sel = 0;
        if (n < w * h)
            snprintf(sdk_note, sizeof sdk_note, "Pasted %d of %d tiles", n, w * h);
        else
            snprintf(sdk_note, sizeof sdk_note, "Pasted %d tiles%s", n, cut ? ", colours cut" : "");
        changed_sheet(0);
        return 1;
    }
    case HID_KEY_DEL:
    case 0x08:
    case 0x7F:
        if (can_change()) {
            sdk_sheet_clear(r1, c1, r2, c2);
            changed_sheet(0);
        }
        return 1;
    default:
        return 0;
    }
}

/* i: the sprite sheet of the ROM into the sheet, in the place of what it has. */
static void import_sheet(int armed)
{
    if (!can_change())
        return;
    if (!armed && !sdk_sheet_empty()) {
        snprintf(sdk_note, sizeof sdk_note, "i again replaces the sheet");
        import_armed = 1;
        return;
    }
    int fmt, chr, count;
    const size_t start = sdk_tiles_start(rom, rom_size, &rom_info, &fmt, &chr);
    size_t chr_len = chr ? (size_t)rom[5] * 8192 : 0;
    if (chr && chr_len > rom_size - start)
        chr_len = rom_size - start;
    const int tile = sdk_tile_bytes(view_fmt);
    const size_t from = sdk_sheet_part(rom_size, start, chr_len, view_off, tile, &count);
    if (count == 0) {
        snprintf(sdk_note, sizeof sdk_note, "No tiles to import here");
        return;
    }
    sdk_sheet_import(rom, rom_size, from, view_fmt, count);
    memset(&pick, 0, sizeof pick);
    sheet_top = 0;
    const int whole = (int)(chr_len / (size_t)tile);
    if (code_kind != SDK_SPRITE_NONE &&
        sdk_tile_colors(view_fmt) > sdk_tile_colors(sdk_chr_format(chr_kind() == SDK_CHR_NES || chr_kind() == SDK_CHR_NES_C)))
        snprintf(sdk_note, sizeof sdk_note, "Imported %d tiles, colours cut", count);
    else if (whole > count)
        snprintf(sdk_note, sizeof sdk_note, "Imported %d/%d tiles at %06X", count, whole, (unsigned)from);
    else
        snprintf(sdk_note, sizeof sdk_note, "Imported %d tiles from %06X", count, (unsigned)from);
    changed_sheet(0);
}

/* ---- the code under the panes ---- */

/* The lines of the tiles block round the tile under the cursor (the pixel of the canvas, or the cell of the
 * sheet), with that tile's own lines in the swapped colours. */
static void code_draw(int top, int rows, int cols)
{
    const sdk_canvas_t *cv = sdk_canvas();
    const int on_canvas = focus == ON_LEFT && cv->open && !(rom && !hidden);
    const int tile = on_canvas ? (cv->row + cv->y / 8) * SDK_SHEET_W + cv->col + cv->x / 8
                               : pick.row * SDK_SHEET_W + pick.col;
    const int c = code_kind == SDK_SPRITE_NES_C || code_kind == SDK_SPRITE_SNES_C;
    const char *file = c ? "main.c  //@tiles" : "main.s  ;@tiles";
    int first = 0, last = 0;
    const int found = !code_err[0] &&
                      sdk_chr_lines(chr_kind(), sdk_edit_text(), sdk_edit_length(), tile, &first, &last);
    char head[96];
    if (found && first == last)
        snprintf(head, sizeof head, "%s  tile %03X  line %d", file, (unsigned)tile, first);
    else if (found)
        snprintf(head, sizeof head, "%s  tile %03X  lines %d-%d", file, (unsigned)tile, first, last);
    else
        snprintf(head, sizeof head, "%s  tile %03X  %s", file, (unsigned)tile,
                 code_err[0] ? "the block cannot be read" : "empty: not in the code yet");
    sdk_title_bar(top, 1, cols, head, 0);
    const int room = rows - 1;
    int from = 0;                                       /* the first line shown, from 1 */
    if (found)
        from = first - (room - (last - first + 1)) / 2;
    for (int i = 0; i < room; i++) {
        const int line = from + i;
        const char *s = NULL;
        int n = 0;
        if (found && line >= 1 && line <= sdk_edit_line_count())
            n = sdk_edit_line(line - 1, &s);
        for (int cut = 0; cut < 4 && n && *s == ' '; cut++)     /* the indent: 16 bytes of C fill the row */
            s++, n--;
        console_write(found && line >= first && line <= last ? "\x1b[0;30;47m" : "\x1b[0m");
        sdk_put_row(top + 1 + i, 1, cols - 1, s ? s : "", n);   /* the last cell stays: no scroll */
        console_write("\x1b[0m");
    }
}

/* ---- the Sprite tab ---- */

void sdk_sprite_draw(int cols, int rows, const char *msg)
{
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    int wide = SHEET_GUTTER + (SDK_SHEET_W * 8 * ((int)ch / 8) + (int)cw - 1) / (int)cw;
    if (wide < 37)                              /* room for the head of the sheet */
        wide = 37;
    const int split = cols >= wide + 1 + 30;    /* both sides; else the side with the keys */
    const int show_rom = rom && !hidden;
    const int left = split ? cols - wide - 1 : focus == ON_LEFT ? cols : 0;
    const int right = split ? cols - wide : focus == ON_SHEET ? 1 : 0;          /* the column of the sheet */
    const int right_width = split ? wide : cols;
    const int code = show_code && code_kind != SDK_SPRITE_NONE ? CODE_ROWS : 0;
    rows -= code;                               /* the panes */
    if (code)
        code_draw(rows + 1, code, cols);
    if (left) {
        if (show_rom) {
            char head[80];
            snprintf(head, sizeof head, "ROM: %s", rom_name);
            sdk_title_bar(1, 1, left, head, focus == ON_LEFT);
            sdk_tiles_draw(1, left, rows, 0, focus == ON_LEFT);
        } else
            canvas_draw(1, left, rows);
    }
    if (split) {
        console_write("\x1b[90m");
        for (int r = 1; r <= rows; r++) {
            sdk_at(r, left + 1);
            console_putc((char)0xB3);
        }
        console_write("\x1b[0m");
    }
    if (right)
        sheet_draw(right, right_width, rows, focus == ON_SHEET);
    if (msg && msg[0])
        text_row(rows, 1, left ? left : right_width, "\x1b[93m", msg);
    sdk_at(1, 1);
    console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    if (left && show_rom)                       /* the pixels last: the console repaints the cells it writes */
        sdk_tiles_paint();
    else if (left)
        canvas_paint(1, left, rows);
    if (right)
        sheet_paint(right);
}

int sdk_sprite_keys(void)
{
    return focus != ON_LEFT ? SDK_KEYS_SHEET : rom && !hidden ? SDK_KEYS_TILES : SDK_KEYS_CANVAS;
}

int sdk_sprite_key(int key, int mods)
{
    sdk_note[0] = 0;
    const int armed = import_armed;
    import_armed = 0;                           /* only the key after the question answers it */
    const int open = sdk_canvas()->open;
    if (key == 0x14 || (key == 9 && (mods & SDK_MOD_CTRL))) {                   /* Ctrl+T, Ctrl+Tab */
        if (focus == ON_LEFT)
            focus = ON_SHEET;
        else if (rom || open) {
            if (!open)
                hidden = 0;
            focus = ON_LEFT;
        } else
            snprintf(sdk_note, sizeof sdk_note, "Enter on a tile opens the canvas");
        return 1;
    }
    if (key == 0x05) {                          /* Ctrl+E: the ROM or the canvas on the left */
        if (!rom)
            snprintf(sdk_note, sizeof sdk_note, "No ROM open (Esc > Open ROM)");
        else {
            hidden = !hidden;
            focus = !hidden || open ? ON_LEFT : ON_SHEET;
        }
        return 1;
    }
    const int on_rom = focus == ON_LEFT && rom && !hidden;
    const int on_canvas = focus == ON_LEFT && !on_rom;
    if (key == 'v' && code_kind != SDK_SPRITE_NONE) {
        show_code = !show_code;
        snprintf(sdk_note, sizeof sdk_note, show_code ? "The code below" : "Code hidden: v shows it");
        return 1;
    }
    if (key == 'i') {
        if (on_canvas)
            snprintf(sdk_note, sizeof sdk_note, "i imports into the sheet (Ctrl+T)");
        else if (rom)
            import_sheet(armed);
        else
            snprintf(sdk_note, sizeof sdk_note, "No ROM open (Esc > Open ROM)");
        return 1;
    }
    if (on_rom)
        return sdk_tiles_key(key, mods);
    if (pen_key(key))
        return 1;
    return on_canvas ? canvas_key(key, mods) : sheet_key(key, mods);
}
