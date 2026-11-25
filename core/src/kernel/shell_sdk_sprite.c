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
#include "shell_sdk_code.h"
#include "shell_sdk_pane.h"
#include "shell_sdk_tiles.h"
#include "usb/hid.h"

#define TILES_GUTTER  7                 /* "004010 ": the offset of a row, as in the dump */
#define CANVAS_GUTTER 4                 /* "010 ": the number of the first tile of a row */
#define FIRST         3                 /* the ANSI row of the first row of tiles */

enum { ON_CANVAS, ON_ROM };

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
static int focus = ON_CANVAS;
static int hidden;                      /* Ctrl+E: the canvas takes the whole tab */
static sdk_grid_t pen;                  /* the cursor and the selection of the canvas */
static int canvas_top, canvas_rows = 1, canvas_scale = 1;

void sdk_sprite_reset(void)
{
    sdk_canvas_reset();
    memset(&pen, 0, sizeof pen);
    canvas_top = 0;
    focus = ON_CANVAS;
    hidden = 0;
}

void sdk_sprite_rom(const uint8_t *data, size_t size, const sdk_rom_t *info, const char *name)
{
    rom = data;
    rom_size = data ? size : 0;
    hidden = 0;
    focus = data ? ON_ROM : ON_CANVAS;
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

/* The colours of a game are not in its tiles: the colour numbers show as greys, 0 as dark blue. */
static void paint_tile(framebuffer_t *fb, const uint8_t pix[64], int fmt, int x0, int y0, int scale)
{
    const int n = sdk_tile_colors(fmt);
    for (int y = 0; y < 8; y++)
        for (int x = 0, e; x < 8; x = e) {              /* a run of one colour is one rectangle */
            const int v = pix[y * 8 + x];
            for (e = x + 1; e < 8 && pix[y * 8 + e] == v; e++)
                ;
            const uint8_t g = v ? (uint8_t)(40 + 215 * v / (n - 1)) : 0;
            gfx_rect(fb, x0 + x * scale, y0 + y * scale, (e - x) * scale, scale,
                     v ? fb_color(fb, g, g, g) : fb_color(fb, 0, 0, 56));
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
    const char *order = view_tall ? "8x8" : "8x16";
    if (code)
        snprintf(b, sizeof b, "t: hex  %s  +/-: row  o: %s", rom_info.kind == SDK_ROM_NES ? "a: ASM" : "b: bpp",
                 order);
    else {
        const long off = cell_offset(cursor.row, cursor.col);
        snprintf(b, sizeof b, "%06X +/- row  o %s  ,. byte%s", (unsigned)(off < 0 ? 0 : off), order,
                 rom_info.kind == SDK_ROM_NES ? "" : "  b bpp");
    }
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
            paint_tile(fb, pix, view_fmt, x0 + c * side, y0 + r * side, view_scale);
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

/* ---- the canvas ---- */

static void canvas_draw(int col, int width, int rows, int full, const char *msg)
{
    uint32_t cw = 8, ch = 16, top = 0;
    console_cell_geometry(&cw, &ch, &top);
    const int pixels = (width - CANVAS_GUTTER) * (int)cw;
    const int text = rows - FIRST > 0 ? rows - FIRST : 1;
    canvas_scale = (int)ch / 8;
    if (full) {                                 /* the whole screen: as large as 16 rows allow */
        const int high = text * (int)ch / (16 * 8);
        if (high > canvas_scale)
            canvas_scale = high;
    }
    if (canvas_scale > pixels / (SDK_CANVAS_W * 8))
        canvas_scale = pixels / (SDK_CANVAS_W * 8);
    if (canvas_scale < 1)
        canvas_scale = 1;
    const int side = 8 * canvas_scale;
    canvas_rows = text * (int)ch / side;
    if (canvas_rows > SDK_CANVAS_H)
        canvas_rows = SDK_CANVAS_H;
    if (canvas_rows < 1)
        canvas_rows = 1;
    if (pen.row < canvas_top)
        canvas_top = pen.row;
    if (pen.row >= canvas_top + canvas_rows)
        canvas_top = pen.row - canvas_rows + 1;
    if (canvas_top > SDK_CANVAS_H - canvas_rows)
        canvas_top = SDK_CANVAS_H - canvas_rows;

    char b[80];
    snprintf(b, sizeof b, "Canvas 16x32%s", !rom ? "  (Esc > Open ROM)" : hidden ? "  (Ctrl+E: show the ROM)" :
                                                                                  "  (Ctrl+T: ROM)");
    sdk_title_bar(1, col, width, b, focus == ON_CANVAS);
    int w, h;
    if (sdk_clip_size(&w, &h))
        snprintf(b, sizeof b, "Cell %d,%d  copied %dx%d tiles", pen.col, pen.row, w, h);
    else
        snprintf(b, sizeof b, "Cell %d,%d  nothing copied", pen.col, pen.row);
    console_write("\x1b[93m");
    sdk_put_row(2, col, width, b, (int)strlen(b));
    console_write("\x1b[90m");
    for (int i = 0; i < text; i++) {
        const int r = (i * (int)ch + side - 1) / side;
        if (r >= canvas_rows || r * side >= (i + 1) * (int)ch)
            continue;
        snprintf(b, sizeof b, "%03X", (unsigned)((canvas_top + r) * SDK_CANVAS_W));
        sdk_put_row(FIRST + i, col, CANVAS_GUTTER, b, (int)strlen(b));
    }

    /* with the whole screen the keys show at the right of the sheet */
    const int sheet = CANVAS_GUTTER + (SDK_CANVAS_W * side + (int)cw - 1) / (int)cw;
    if (full && width - sheet - 2 >= 30) {
        static const char *const help[] = {
            "Arrows       move (Shift selects)",
            "Ctrl+C, X    copy, cut",
            "Ctrl+V       paste at the cursor",
            "Del          empty the cells",
            "Ctrl+A       select all",
            "Ctrl+E       hide or show the ROM",
            "Ctrl+T       canvas or ROM",
        };
        console_write("\x1b[0m");
        for (int i = 0; i < (int)(sizeof help / sizeof help[0]) && FIRST + i < rows; i++)
            sdk_put_row(FIRST + i, col + sheet + 2, width - sheet - 2, help[i], (int)strlen(help[i]));
    }
    const char *hint = "Ctrl+V: paste  Del: empty  Ctrl+E";
    if (msg && msg[0]) {
        console_write("\x1b[93m");
        sdk_put_row(rows, col, width, msg, (int)strlen(msg));
        console_write("\x1b[0m");
    } else
        sdk_pane_status(col, width, rows, hint, focus == ON_CANVAS);
}

static void canvas_paint(int col)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb)
        return;
    uint32_t cw, ch, top;
    console_cell_geometry(&cw, &ch, &top);
    const int side = 8 * canvas_scale;
    const int x0 = (col - 1 + CANVAS_GUTTER) * (int)cw;
    const int y0 = (int)top + (FIRST - 1) * (int)ch;
    for (int r = 0; r < canvas_rows; r++)
        for (int c = 0; c < SDK_CANVAS_W; c++) {
            const sdk_tile_t *t = sdk_canvas_tile(canvas_top + r, c);
            const int x = x0 + c * side, y = y0 + r * side;
            if (t->used)
                paint_tile(fb, t->pix, t->fmt, x, y, canvas_scale);
            else {                              /* an empty cell: its edges show the grid */
                gfx_rect(fb, x, y, side, side, fb_color(fb, 56, 56, 56));
                gfx_rect(fb, x + 1, y + 1, side - 1, side - 1, fb_color(fb, 16, 16, 16));
            }
        }
    paint_cursor(fb, &pen, x0, y0, side, canvas_top, canvas_rows, focus == ON_CANVAS);
}

static int canvas_key(int key, int mods)
{
    const int shift = (mods & SDK_MOD_SHIFT) != 0;
    int dr = 0, dc = 0;
    switch (key) {
    case HID_KEY_UP: dr = -1; break;
    case HID_KEY_DOWN: dr = 1; break;
    case HID_KEY_LEFT: dc = -1; break;
    case HID_KEY_RIGHT: dc = 1; break;
    case HID_KEY_PGUP: dr = -canvas_rows; break;
    case HID_KEY_PGDN: dr = canvas_rows; break;
    case HID_KEY_HOME: dc = -SDK_CANVAS_W; break;
    case HID_KEY_END: dc = SDK_CANVAS_W; break;
    default: break;
    }
    if (dr || dc) {
        sdk_grid_move(&pen, dr, dc, shift, SDK_CANVAS_H, SDK_CANVAS_W);
        return 1;
    }
    int r1, c1, r2, c2, w, h;
    sdk_grid_rect(&pen, &r1, &c1, &r2, &c2);
    switch (key) {
    case 0x01:                                  /* Ctrl+A */
        pen.sel = 1;
        pen.arow = pen.acol = 0;
        pen.row = SDK_CANVAS_H - 1;
        pen.col = SDK_CANVAS_W - 1;
        return 1;
    case 0x03:                                  /* Ctrl+C */
    case 0x18:                                  /* Ctrl+X */
        sdk_canvas_copy(r1, c1, r2, c2);
        if (key == 0x18)
            sdk_canvas_clear(r1, c1, r2, c2);
        sdk_clip_size(&w, &h);
        snprintf(sdk_note, sizeof sdk_note, "%s %dx%d tiles", key == 0x18 ? "Cut" : "Copied", w, h);
        return 1;
    case 0x16: {                                /* Ctrl+V: at the first cell of the selection */
        if (!sdk_clip_size(&w, &h)) {
            snprintf(sdk_note, sizeof sdk_note, "Nothing to paste: copy tiles first");
            return 1;
        }
        const int n = sdk_canvas_paste(r1, c1);
        pen.row = r1;
        pen.col = c1;
        pen.sel = 0;
        if (n < w * h)
            snprintf(sdk_note, sizeof sdk_note, "Pasted %d of %d tiles", n, w * h);
        else
            snprintf(sdk_note, sizeof sdk_note, "Pasted %d tiles", n);
        return 1;
    }
    case HID_KEY_DEL:
    case 0x08:
    case 0x7F:
        sdk_canvas_clear(r1, c1, r2, c2);
        return 1;
    default:
        return 0;
    }
}

/* ---- the Sprite tab ---- */

void sdk_sprite_draw(int cols, int rows, const char *msg)
{
    const int split = rom && !hidden && cols >= 40;
    if (!split)
        focus = ON_CANVAS;
    const int lw = split ? cols / 2 : cols;
    canvas_draw(1, lw - 1, rows, !split, msg);
    if (split) {
        console_write("\x1b[90m");
        for (int r = 1; r <= rows; r++) {
            sdk_at(r, lw);
            console_putc((char)0xB3);
        }
        console_write("\x1b[0m");
        char head[80];
        snprintf(head, sizeof head, "ROM: %s", rom_name);
        sdk_title_bar(1, lw + 1, cols - lw - 1, head, focus == ON_ROM);
        sdk_tiles_draw(lw + 1, cols - lw - 1, rows, 0, focus == ON_ROM);
    }
    sdk_at(1, 1);
    console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    canvas_paint(1);                            /* last: the console repaints the cells it writes */
    if (split)
        sdk_tiles_paint();
}

int sdk_sprite_key(int key, int mods)
{
    sdk_note[0] = 0;
    const int swap = key == 0x14 || (key == 9 && (mods & SDK_MOD_CTRL));    /* Ctrl+T, Ctrl+Tab */
    if (swap || key == 0x05) {                                              /* Ctrl+E: hide, show the ROM */
        if (!rom)
            snprintf(sdk_note, sizeof sdk_note, "No ROM open (Esc > Open ROM)");
        else if (swap) {
            focus = hidden || focus == ON_CANVAS ? ON_ROM : ON_CANVAS;
            hidden = 0;
        } else {
            hidden = !hidden;
            focus = ON_CANVAS;
        }
        return 1;
    }
    if (rom && !hidden && focus == ON_ROM)
        return sdk_tiles_key(key, mods);
    return canvas_key(key, mods);
}
