/*
 * Shell SDK: the Code tab (see shell_sdk_code.h). Original code.
 */
#include "shell_sdk_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/fb.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "gfx/draw.h"
#include "shell_sdk_dis.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_rom.h"
#include "shell_sdk_tiles.h"
#include "usb/hid.h"

#define ROM_MAX_BYTES (8u * 1024 * 1024)

enum { FOCUS_EDITOR, FOCUS_ROM };

static uint8_t *rom;                    /* the open ROM (NULL: none) */
static size_t rom_size;
static sdk_rom_t rom_info;
static char rom_name[48];
static int focus;
static int top_line, left_col;          /* the scroll of the editor */
static int cur_row = 1, cur_col = 1;    /* the text cursor on the screen (ANSI row and column), set when it is drawn */
static int page_lines = 1;              /* the lines of a page of the editor, set when it is drawn */
static char note[48];                   /* "Copied 12 bytes": shown until the next key */
static int hex_bpr, hex_page;          /* bytes in a row and rows shown, set when the dump is drawn */
static size_t hex_row;                  /* the first row of the dump shown (a row is bpr bytes) */
static int asm_on;                      /* the ROM side shows the ASM listing, not the dump */
static char *asm_text;                  /* the listing of the open ROM, made the first time it is asked */
static char **asm_line;                 /* where each line of the listing starts (the lines end with 0) */
static int asm_lines, asm_top, asm_page;
static int tiles_on;                    /* the ROM side shows the bytes as 8x8 tiles */
static size_t tiles_off;                /* the offset of the first tile shown */
static int tiles_fmt, tiles_chr;        /* how the tiles are read; the view started at the CHR ROM */
static int tiles_col, tiles_rows = 1;   /* where the tiles are on the screen, set when the view is drawn */

#define TILES_GUTTER 7                  /* "004010 ": the offset of a row, as in the dump */
#define TILES_FIRST  3                  /* the ANSI row of the first row of tiles */

static void at(int row, int col)
{
    char b[40];
    snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
    console_write(b);
}

static void free_listing(void)
{
    free(asm_line);
    free(asm_text);
    asm_line = NULL;
    asm_text = NULL;
    asm_lines = asm_top = 0;
    asm_on = 0;
}

/* Makes the listing of the open ROM and cuts it in lines. Returns 0, or -1 with the reason in err. */
static int make_listing(char *err, size_t en)
{
    char *text = NULL;
    size_t len = 0;
    if (sdk_dis(rom, rom_size, &text, &len, err, en) != 0)
        return -1;
    int n = 1;
    for (size_t i = 0; i < len; i++)
        n += text[i] == '\n';
    char **line = malloc((size_t)n * sizeof *line);
    if (!line) {
        free(text);
        snprintf(err, en, "not enough memory for the listing");
        return -1;
    }
    line[0] = text;
    for (size_t i = 0, k = 1; i < len; i++)
        if (text[i] == '\n') {
            text[i] = 0;
            line[k++] = text + i + 1;
        }
    asm_text = text;
    asm_line = line;
    asm_lines = n;
    return 0;
}

/* The ROM goes away: the editor takes the whole tab. The text stays. */
static void close_rom(void)
{
    free_listing();
    tiles_on = 0;
    free(rom);
    rom = NULL;
    rom_size = 0;
    rom_name[0] = 0;
    focus = FOCUS_EDITOR;
    left_col = 0;
    hex_row = 0;
}

void sdk_code_reset(void)
{
    close_rom();
    top_line = 0;
    sdk_edit_reset();
}

int sdk_code_has_rom(void) { return rom != NULL; }
const char *sdk_code_rom_name(void) { return rom_name; }

int sdk_code_open_rom(const char *path, char *err, size_t en)
{
    fat_entry_t e;
    if (fat_find(path, &e) != 0 || e.is_dir) {
        snprintf(err, en, "%s: not found", path);
        return -1;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (e.size > ROM_MAX_BYTES) {
        snprintf(err, en, "%s: more than 8 MB", path);
        return -1;
    }
    if (fat_load(&e, &data, &len) != 0) {
        snprintf(err, en, "%s", fat_error());
        return -1;
    }
    sdk_rom_t info;
    if (sdk_rom_probe(data, len, &info) != 0) {
        snprintf(err, en, "%s", info.lines ? info.line[0] : "not a ROM");
        free(data);
        return -1;
    }
    free_listing();
    free(rom);
    rom = data;
    rom_size = len;
    rom_info = info;
    const char *base = strrchr(path, '/');
    snprintf(rom_name, sizeof rom_name, "%s", base ? base + 1 : path);
    focus = FOCUS_EDITOR;
    hex_row = 0;
    tiles_on = 0;
    tiles_off = sdk_tiles_start(rom, rom_size, &rom_info, &tiles_fmt, &tiles_chr);
    return 0;
}

/* ---- drawing ---- */

#define GUTTER 4

/* One row of a pane: text cut to width cells and padded with spaces. */
static void put_row(int row, int col, int width, const char *s, int n)
{
    char line[256];
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    if (width < 1)
        return;
    snprintf(line, sizeof line, "%-*.*s", width, n < width ? n : width, s);
    at(row, col);
    console_write(line);
}

/* A row of text where the cells sc to ec (not included) are selected: the swapped colours. */
static void text_row(int row, int col, int width, const char *s, int n, int sc, int ec)
{
    char line[256];
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    if (width < 1)
        return;
    snprintf(line, sizeof line, "%-*.*s", width, n < width ? n : width, s);
    at(row, col);
    if (sc < 0)
        sc = 0;
    if (ec > width)
        ec = width;
    if (ec <= sc) {
        console_write(line);
        return;
    }
    char keep = line[sc];
    line[sc] = 0;
    console_write(line);
    line[sc] = keep;
    console_write("\x1b[7m");
    keep = line[ec];
    line[ec] = 0;
    console_write(line + sc);
    line[ec] = keep;
    console_write("\x1b[0m");
    console_write(line + ec);
}

static void title_bar(int row, int col, int width, const char *text, int active)
{
    char line[256];
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    snprintf(line, sizeof line, " %-*.*s", width - 1, width - 1, text);
    at(row, col);
    console_write(active ? "\x1b[7m" : "\x1b[0m\x1b[96m");
    console_write(line);
    console_write("\x1b[0m");
}

static void draw_editor(int col, int width, int rows, int split, const char *msg)
{
    char head[64];
    snprintf(head, sizeof head, "Code%s", rom ? "  (Ctrl+T: ROM, Ctrl+W: close)" : "");
    title_bar(1, col, width, head, focus == FOCUS_EDITOR);

    const int h = rows - 2;                         /* rows 2 to rows-1 */
    const int tw = width - GUTTER;
    int line, c;
    sdk_edit_pos(&line, &c);
    if (line < top_line)
        top_line = line;
    if (line >= top_line + h)
        top_line = line - h + 1;
    if (c < left_col)
        left_col = c;
    if (c >= left_col + tw - 1)
        left_col = c - tw + 2;
    if (left_col < 0)
        left_col = 0;

    page_lines = h > 2 ? h - 1 : 1;
    size_t sel_a = 0, sel_b = 0;
    const int has_sel = sdk_edit_selection(&sel_a, &sel_b);
    size_t off = sdk_edit_line_offset(top_line);            /* the offset of the line being drawn */
    const int total = sdk_edit_line_count();
    for (int i = 0; i < h; i++) {
        const int l = top_line + i;
        char num[16];
        console_write("\x1b[90m");
        if (l < total)
            snprintf(num, sizeof num, "%*d ", GUTTER - 1, l + 1);
        else
            snprintf(num, sizeof num, "%*s ", GUTTER - 1, "~");
        put_row(2 + i, col, GUTTER, num, GUTTER);
        console_write("\x1b[0m");
        const char *s;
        int n = sdk_edit_line(l, &s);
        if (!s) {
            put_row(2 + i, col + GUTTER, tw, "", 0);
            continue;
        }
        const size_t next = off + (size_t)n + 1;            /* the next line starts here */
        int sc = 0, ec = 0;                                 /* the selected cells of the row */
        if (has_sel && sel_b > off && sel_a < next) {
            sc = (int)(sel_a > off ? sel_a - off : 0) - left_col;
            ec = (int)((sel_b < next ? sel_b : next) - off) - left_col;
        }
        off = next;
        if (left_col >= n) {
            s = "";
            n = 0;
        } else {
            s += left_col;
            n -= left_col;
        }
        text_row(2 + i, col + GUTTER, tw, s, n, sc, ec);
    }

    char st[96];
    snprintf(st, sizeof st, "Ln %d  Col %d  %u bytes", line + 1, c + 1, (unsigned)sdk_edit_length());
    console_write("\x1b[96m");
    put_row(rows, col, width, msg && msg[0] ? msg : note[0] && !(split && focus == FOCUS_ROM) ? note : st, 200);
    console_write("\x1b[0m");
    cur_row = 1 + 1 + (line - top_line);                    /* where the text cursor is on the screen */
    cur_col = col + GUTTER + (c - left_col);
}

/* The last row of the ROM pane: the answer to the last key (a, b), or else the hint. */
static void rom_status(int col, int width, int rows, const char *hint)
{
    const int answer = focus == FOCUS_ROM && note[0];
    const char *s = answer ? note : hint;
    console_write(answer ? "\x1b[93m" : "\x1b[96m");
    put_row(rows, col, width, s, (int)strlen(s));
    console_write("\x1b[0m");
}

/* The ASM side: the rows 2 to rows-1 are the listing, from asm_top. */
static void draw_listing(int col, int width, int rows)
{
    const int h = rows - 2;
    asm_page = h > 1 ? h - 1 : 1;
    for (int i = 0; i < h; i++) {
        const int l = asm_top + i;
        const char *s = l < asm_lines ? asm_line[l] : "";
        const int n = (int)strlen(s);
        console_write(n && (s[n - 1] == ':' || s[0] == ';') ? "\x1b[93m" : "\x1b[0m");     /* a label stands out */
        put_row(2 + i, col, width, s, n);
    }
    char st[64];
    snprintf(st, sizeof st, "ASM  line %d of %d  a: hex", asm_top + 1, asm_lines);
    rom_status(col, width, rows, st);
}

/* The tile side: its text (the offsets); paint_tiles draws the pixels after all the text. */
static void draw_tiles(int col, int width, int rows)
{
    tiles_col = col;
    tiles_rows = rows - TILES_FIRST > 0 ? rows - TILES_FIRST : 1;
    char b[64];
    snprintf(b, sizeof b, "%s  %s", rom_info.kind != SDK_ROM_NES ? "ROM" : tiles_chr ? "CHR ROM" : "PRG (CHR RAM)",
             sdk_tile_name(tiles_fmt));
    console_write("\x1b[93m");
    put_row(2, col, width, b, (int)strlen(b));
    console_write("\x1b[90m");
    const size_t step = (size_t)sdk_tile_bytes(tiles_fmt) * SDK_TILES_ROW;
    for (int i = 0; i < tiles_rows; i++) {
        const size_t off = tiles_off + (size_t)i * step;
        if (off < rom_size) {
            snprintf(b, sizeof b, "%06X", (unsigned)off);
            put_row(TILES_FIRST + i, col, TILES_GUTTER, b, (int)strlen(b));
        }
    }
    snprintf(b, sizeof b, "t: hex  %s  Left/Right: 1 byte", rom_info.kind == SDK_ROM_NES ? "a: ASM" : "b: bpp");
    rom_status(col, width, rows, b);
}

/* Pixels on the framebuffer: a row of tiles is as high as a row of text (when the pane is wide enough). The
 * colours of a game are not in its tiles: the colour numbers show as greys on dark blue. */
static void paint_tiles(int width)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb)
        return;
    uint32_t cw, ch, top;
    console_cell_geometry(&cw, &ch, &top);
    int scale = (int)ch / 8;
    const int widest = (width - TILES_GUTTER) * (int)cw / (SDK_TILES_ROW * 8);
    if (scale > widest)
        scale = widest;
    if (scale < 1)
        return;
    static uint32_t color[256];
    const int n = sdk_tile_colors(tiles_fmt);
    color[0] = fb_color(fb, 0, 0, 56);
    for (int v = 1; v < n; v++) {
        const uint8_t g = (uint8_t)(40 + 215 * v / (n - 1));
        color[v] = fb_color(fb, g, g, g);
    }
    const int x0 = (tiles_col - 1 + TILES_GUTTER) * (int)cw;
    const size_t step = (size_t)sdk_tile_bytes(tiles_fmt);
    for (int r = 0; r < tiles_rows; r++) {
        const int y0 = (int)top + (TILES_FIRST - 1 + r) * (int)ch;
        for (int t = 0; t < SDK_TILES_ROW; t++) {
            const size_t off = tiles_off + (size_t)(r * SDK_TILES_ROW + t) * step;
            if (off >= rom_size)
                return;
            uint8_t pix[64];
            sdk_tile_decode(rom, rom_size, off, tiles_fmt, pix);
            for (int y = 0; y < 8; y++)
                for (int x = 0, e; x < 8; x = e) {          /* a run of one colour is one rectangle */
                    for (e = x + 1; e < 8 && pix[y * 8 + e] == pix[y * 8 + x]; e++)
                        ;
                    gfx_rect(fb, x0 + (t * 8 + x) * scale, y0 + y * scale, (e - x) * scale, scale,
                             color[pix[y * 8 + x]]);
                }
        }
    }
}

static void draw_rom(int col, int width, int rows)
{
    char head[80];
    snprintf(head, sizeof head, "ROM: %s", rom_name);
    title_bar(1, col, width, head, focus == FOCUS_ROM);
    if (asm_on) {
        draw_listing(col, width, rows);
        return;
    }
    if (tiles_on) {
        draw_tiles(col, width, rows);
        return;
    }

    int row = 2;
    console_write("\x1b[93m");
    if (rom_info.title[0]) {
        put_row(row++, col, width, rom_info.title, (int)strlen(rom_info.title));
    }
    for (int i = 0; i < rom_info.lines && row < rows; i++)
        put_row(row++, col, width, rom_info.line[i], (int)strlen(rom_info.line[i]));
    char sz[40];
    snprintf(sz, sizeof sz, "File %u KB", (unsigned)((rom_size + 1023) / 1024));
    if (row < rows)
        put_row(row++, col, width, sz, (int)strlen(sz));
    console_write("\x1b[0m");
    row++;

    const int bpr = sdk_rom_hex_bytes(width);
    const size_t total_rows = (rom_size + (size_t)bpr - 1) / (size_t)bpr;
    const int h = rows - row;                       /* up to the row before the last */
    hex_bpr = bpr;
    hex_page = h > 0 ? h : 1;
    for (int i = 0; i < h; i++) {
        char line[96];
        const size_t r = hex_row + (size_t)i;
        if (r < total_rows) {
            sdk_rom_hex_row(rom, rom_size, r * (size_t)bpr, width, line, sizeof line);
            console_write("\x1b[0m");
        } else
            line[0] = 0;
        put_row(row + i, col, width, line, (int)strlen(line));
    }
    char st[48];
    snprintf(st, sizeof st, "Offset %06X  a: ASM  t: tiles", (unsigned)(hex_row * (size_t)bpr));
    rom_status(col, width, rows, st);
}

void sdk_code_draw(int cols, int rows, const char *msg)
{
    const int split = rom != NULL && cols >= 40;
    const int lw = split ? cols / 2 : cols;         /* the left side: up to and with the divider */
    draw_editor(1, lw - 1, rows, split, msg);
    if (!split) {
        at(cur_row, cur_col);
        console_cursor_mode(CONSOLE_CURSOR_BLOCK);
        return;
    }
    console_write("\x1b[90m");
    for (int r = 1; r <= rows; r++) {
        at(r, lw);
        console_putc((char)0xB3);
    }
    console_write("\x1b[0m");
    draw_rom(lw + 1, cols - lw - 1, rows);
    if (focus == FOCUS_EDITOR) {
        at(cur_row, cur_col);                               /* a solid square, the letter under it swapped */
        console_cursor_mode(CONSOLE_CURSOR_BLOCK);
    } else {
        at(1, 1);
        console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    }
    if (tiles_on)
        paint_tiles(cols - lw - 1);                         /* last: the console repaints the cells it writes */
}

/* ---- keys ---- */

static int listing_key(int key)
{
    const int last = asm_lines > asm_page ? asm_lines - asm_page : 0;
    if (key == HID_KEY_UP)
        asm_top--;
    else if (key == HID_KEY_DOWN)
        asm_top++;
    else if (key == HID_KEY_PGUP)
        asm_top -= asm_page;
    else if (key == HID_KEY_PGDN)
        asm_top += asm_page;
    else if (key == HID_KEY_HOME)
        asm_top = 0;
    else if (key == HID_KEY_END)
        asm_top = last;
    else
        return 0;
    asm_top = asm_top > last ? last : asm_top;
    asm_top = asm_top < 0 ? 0 : asm_top;
    return 1;
}

static int tiles_key(int key)
{
    int move = -1;
    if (key == HID_KEY_UP)
        move = SDK_TILES_UP;
    else if (key == HID_KEY_DOWN)
        move = SDK_TILES_DOWN;
    else if (key == HID_KEY_PGUP)
        move = SDK_TILES_PGUP;
    else if (key == HID_KEY_PGDN)
        move = SDK_TILES_PGDN;
    else if (key == HID_KEY_LEFT)
        move = SDK_TILES_LEFT;
    else if (key == HID_KEY_RIGHT)
        move = SDK_TILES_RIGHT;
    else if (key == HID_KEY_END)
        move = SDK_TILES_END;
    if (move >= 0) {
        tiles_off = sdk_tiles_move(tiles_off, rom_size, tiles_fmt, tiles_rows, move);
        return 1;
    }
    if (key == HID_KEY_HOME) {                  /* back where the view started */
        int fmt;
        tiles_off = sdk_tiles_start(rom, rom_size, &rom_info, &fmt, &tiles_chr);
        return 1;
    }
    if (key == 'b') {
        if (sdk_tile_next(tiles_fmt) == tiles_fmt)
            snprintf(note, sizeof note, "NES tiles are 2bpp");
        tiles_fmt = sdk_tile_next(tiles_fmt);
        return 1;
    }
    return 0;
}

static int rom_key(int key)
{
    if (key == 't') {                           /* hex or tiles */
        tiles_on = !tiles_on;
        asm_on = 0;
        return 1;
    }
    if (key == 'a') {                           /* hex or ASM */
        char err[sizeof note];
        if (!asm_on && !asm_text && make_listing(err, sizeof err) != 0)
            snprintf(note, sizeof note, "%s", err);
        else {
            asm_on = !asm_on;
            tiles_on = 0;
        }
        return 1;
    }
    if (asm_on)
        return listing_key(key);
    if (tiles_on)
        return tiles_key(key);
    const size_t bpr = (size_t)(hex_bpr > 0 ? hex_bpr : 16);
    const size_t total = (rom_size + bpr - 1) / bpr;
    const size_t page = (size_t)(hex_page > 0 ? hex_page : 1);
    const size_t last = total > page ? total - page : 0;
    if (key == HID_KEY_UP)
        hex_row = hex_row ? hex_row - 1 : 0;
    else if (key == HID_KEY_DOWN)
        hex_row = hex_row < last ? hex_row + 1 : last;
    else if (key == HID_KEY_PGUP)
        hex_row = hex_row > page ? hex_row - page : 0;
    else if (key == HID_KEY_PGDN)
        hex_row = hex_row + page < last ? hex_row + page : last;
    else if (key == HID_KEY_HOME)
        hex_row = 0;
    else if (key == HID_KEY_END)
        hex_row = last;
    else
        return 0;
    return 1;
}

/* A move of the cursor: Shift keeps the selection (and grows it), no Shift ends it. */
static void move(int mods)
{
    sdk_edit_anchor(mods & SDK_MOD_SHIFT ? 1 : 0);
}

static int editor_key(int key, int mods)
{
    const int ctrl = (mods & SDK_MOD_CTRL) != 0, alt = (mods & SDK_MOD_ALT) != 0;
    note[0] = 0;
    switch (key) {
    case 0x01: sdk_edit_select_all(); return 1;                  /* Ctrl+A */
    case 0x03:                                                    /* Ctrl+C: copy the selection or the line */
    case 0x18: {                                                  /* Ctrl+X: cut it */
        const int sel = sdk_edit_copy(key == 0x18) > 0;
        if (sel)
            snprintf(note, sizeof note, "%s %u bytes", key == 0x18 ? "Cut" : "Copied", (unsigned)sdk_edit_clip_length());
        return 1;
    }
    case 0x16:                                                    /* Ctrl+V: paste */
        snprintf(note, sizeof note, "Pasted %d bytes", sdk_edit_paste());
        return 1;
    case 0x04: sdk_edit_duplicate_line(); return 1;              /* Ctrl+D: duplicate the line */
    case 0x1A:                                                    /* Ctrl+Z: undo; with Shift: redo */
    case 0x19:                                                    /* Ctrl+Y: redo */
        if (key == 0x1A && !(mods & SDK_MOD_SHIFT))
            snprintf(note, sizeof note, "%s", sdk_edit_undo() ? "Undo" : "Nothing to undo");
        else
            snprintf(note, sizeof note, "%s", sdk_edit_redo() ? "Redo" : "Nothing to redo");
        return 1;
    case HID_KEY_UP:
        move(mods);
        if (ctrl && alt)
            sdk_edit_doc_start();
        else if (alt)
            sdk_edit_page_up(page_lines);
        else
            sdk_edit_up();
        return 1;
    case HID_KEY_DOWN:
        move(mods);
        if (ctrl && alt)
            sdk_edit_doc_end();
        else if (alt)
            sdk_edit_page_down(page_lines);
        else
            sdk_edit_down();
        return 1;
    case HID_KEY_LEFT:
        move(mods);
        if (ctrl && alt)
            sdk_edit_home();
        else if (ctrl)
            sdk_edit_word_left();
        else
            sdk_edit_left();
        return 1;
    case HID_KEY_RIGHT:
        move(mods);
        if (ctrl && alt)
            sdk_edit_end();
        else if (ctrl)
            sdk_edit_word_right();
        else
            sdk_edit_right();
        return 1;
    case HID_KEY_HOME: move(mods); sdk_edit_home(); return 1;
    case HID_KEY_END: move(mods); sdk_edit_end(); return 1;
    case HID_KEY_PGUP: move(mods); sdk_edit_page_up(page_lines); return 1;
    case HID_KEY_PGDN: move(mods); sdk_edit_page_down(page_lines); return 1;
    case HID_KEY_DEL: sdk_edit_delete(); return 1;
    case 0x08:
    case 0x7F: sdk_edit_backspace(); return 1;
    default: return sdk_edit_type(key);                          /* typing replaces the selection */
    }
}

int sdk_code_key(int key, int mods)
{
    note[0] = 0;
    const int swap = key == 0x14 || (key == 9 && (mods & SDK_MOD_CTRL));    /* Ctrl+T, Ctrl+Tab */
    if (swap || key == 0x17) {                                              /* Ctrl+W: close the ROM */
        if (!rom)
            snprintf(note, sizeof note, "No ROM open (Esc > Open ROM)");
        else if (swap)
            focus = focus == FOCUS_EDITOR ? FOCUS_ROM : FOCUS_EDITOR;
        else {
            close_rom();
            snprintf(note, sizeof note, "ROM closed");
        }
        return 1;
    }
    if (rom && focus == FOCUS_ROM)
        return rom_key(key);
    return editor_key(key, mods);
}
