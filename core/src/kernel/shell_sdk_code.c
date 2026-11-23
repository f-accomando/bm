/*
 * Shell SDK: the Code tab (see shell_sdk_code.h). Original code.
 */
#include "shell_sdk_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fat.h"
#include "gfx/console.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_rom.h"
#include "usb/hid.h"

#define ROM_MAX_BYTES (8u * 1024 * 1024)

enum { FOCUS_EDITOR, FOCUS_ROM };

static uint8_t *rom;                    /* the open ROM (NULL: none) */
static size_t rom_size;
static sdk_rom_t rom_info;
static char rom_name[48];
static int focus;
static int top_line, left_col;          /* the scroll of the editor */
static int page_lines = 1;              /* the lines of a page of the editor, set when it is drawn */
static char note[48];                   /* "Copied 12 bytes": shown until the next key */
static int hex_bpr, hex_page;          /* bytes in a row and rows shown, set when the dump is drawn */
static size_t hex_row;                  /* the first row of the dump shown (a row is bpr bytes) */

static void at(int row, int col)
{
    char b[40];
    snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
    console_write(b);
}

void sdk_code_reset(void)
{
    free(rom);
    rom = NULL;
    rom_size = 0;
    rom_name[0] = 0;
    focus = FOCUS_EDITOR;
    top_line = left_col = 0;
    hex_row = 0;
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
    free(rom);
    rom = data;
    rom_size = len;
    rom_info = info;
    const char *base = strrchr(path, '/');
    snprintf(rom_name, sizeof rom_name, "%s", base ? base + 1 : path);
    focus = FOCUS_EDITOR;
    hex_row = 0;
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

static void draw_editor(int col, int width, int rows, int ctrl_hint, const char *msg)
{
    char head[64];
    snprintf(head, sizeof head, "Code%s", rom ? "   (Ctrl+Tab: ROM)" : "");
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
    put_row(rows, col, width, msg && msg[0] ? msg : note[0] ? note : st, 200);
    console_write("\x1b[0m");
    (void)ctrl_hint;
    if (focus == FOCUS_EDITOR)
        at(1 + 1 + (line - top_line), col + GUTTER + (c - left_col));      /* the cursor shows where the text goes */
}

static void draw_rom(int col, int width, int rows)
{
    char head[80];
    snprintf(head, sizeof head, "ROM: %s", rom_name);
    title_bar(1, col, width, head, focus == FOCUS_ROM);

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
    snprintf(st, sizeof st, "Offset %06X of %06X", (unsigned)(hex_row * (size_t)bpr), (unsigned)rom_size);
    console_write("\x1b[96m");
    put_row(rows, col, width, st, (int)strlen(st));
    console_write("\x1b[0m");
}

void sdk_code_draw(int cols, int rows, const char *msg)
{
    const int split = rom != NULL && cols >= 40;
    const int lw = split ? cols / 2 : cols;         /* the left side: up to and with the divider */
    draw_editor(1, lw - 1, rows, split, msg);
    if (!split)
        return;
    console_write("\x1b[90m");
    for (int r = 1; r <= rows; r++) {
        at(r, lw);
        console_putc((char)0xB3);
    }
    console_write("\x1b[0m");
    draw_rom(lw + 1, cols - lw - 1, rows);
    if (focus == FOCUS_ROM)
        at(1, 1);
}

/* ---- keys ---- */

static int rom_key(int key)
{
    const size_t bpr = (size_t)(hex_bpr > 0 ? hex_bpr : 16);
    const size_t total = (rom_size + bpr - 1) / bpr;
    const size_t page = (size_t)(hex_page > 0 ? hex_page : 1);
    const size_t last = total > page ? total - page : 0;
    if (key == HID_KEY_UP || key == 'w')
        hex_row = hex_row ? hex_row - 1 : 0;
    else if (key == HID_KEY_DOWN || key == 's')
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
    case 0x19: sdk_edit_duplicate_line(); return 1;              /* Ctrl+Y: duplicate the line */
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
    if (key == 9 && (mods & SDK_MOD_CTRL)) {                    /* Ctrl+Tab */
        if (rom)
            focus = focus == FOCUS_EDITOR ? FOCUS_ROM : FOCUS_EDITOR;
        return 1;
    }
    if (rom && focus == FOCUS_ROM)
        return rom_key(key);
    return editor_key(key, mods);
}
