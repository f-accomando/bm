/*
 * Shell SDK: the Code tab (see shell_sdk_code.h). Original code.
 */
#include "shell_sdk_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fat.h"
#include "gfx/console.h"
#include "shell_sdk_dis.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_pane.h"
#include "shell_sdk_rom.h"
#include "shell_sdk_sprite.h"
#include "shell_sdk_view.h"
#include "usb/hid.h"

#define ROM_MAX_BYTES (8u * 1024 * 1024)

enum { FOCUS_EDITOR, FOCUS_ROM };

static uint8_t *rom;                    /* the open ROM (NULL: none) */
static size_t rom_size;
static sdk_rom_t rom_info;
static char rom_name[48];
static int focus;
static int rom_hidden;                  /* Ctrl+E: the editor takes the whole tab */
static int top_line, left_col;          /* the scroll of the editor */
static int cur_row = 1, cur_col = 1;    /* the text cursor on the screen (ANSI row and column), set when it is drawn */
static int page_lines = 1;              /* the lines of a page of the editor, set when it is drawn */
static int hex_bpr = 8, hex_width = 39, hex_page = 1;  /* bytes in a row, cells and rows, set when drawn */
static sdk_view_t hex_view, asm_view;   /* the cursor in the dump and in the listing: read and copy only */
static int rom_cur_row, rom_cur_col;    /* where that cursor is on the screen */
static int asm_on;                      /* the ROM side shows the ASM listing, not the dump */
static char *asm_text;                  /* the listing of the open ROM, made the first time it is asked */
static char **asm_line;                 /* where each line of the listing starts (the lines end with 0) */
static int asm_lines, asm_page = 1;
static sdk_dis_info_t asm_info;         /* the bank it shows, for a ROM with banks */
static int tiles_on;                    /* the ROM side shows the bytes as 8x8 tiles */

static void free_listing(void)
{
    free(asm_line);
    free(asm_text);
    asm_line = NULL;
    asm_text = NULL;
    asm_lines = 0;
    asm_on = 0;
}

/* Makes the listing of the open ROM (bank -1: the first one to show) and cuts it in lines. Returns 0, or -1
 * with the reason in err; the listing before stays then. */
static int make_listing(int bank, char *err, size_t en)
{
    char *text = NULL;
    size_t len = 0;
    sdk_dis_info_t info;
    if (sdk_dis(rom, rom_size, bank, &text, &len, &info, err, en) != 0)
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
    free(asm_line);
    free(asm_text);
    asm_text = text;
    asm_line = line;
    asm_lines = n;
    sdk_view_reset(&asm_view, n);
    asm_info = info;
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
    rom_hidden = 0;
    left_col = 0;
    sdk_view_reset(&hex_view, 0);
    sdk_sprite_rom(NULL, 0, NULL, NULL);
}

void sdk_code_close_rom(void) { close_rom(); }

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
    rom_hidden = 0;
    sdk_view_reset(&hex_view, 0);
    tiles_on = 0;
    sdk_sprite_rom(rom, rom_size, &rom_info, rom_name);
    return 0;
}

/* ---- drawing ---- */

#define GUTTER 4

/* A row of text where the cells sc to ec (not included) are selected: the swapped colours. */
static void text_row(int row, int col, int width, const char *s, int n, int sc, int ec)
{
    char line[256];
    if (width > (int)sizeof line - 1)
        width = (int)sizeof line - 1;
    if (width < 1)
        return;
    snprintf(line, sizeof line, "%-*.*s", width, n < width ? n : width, s);
    sdk_at(row, col);
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

static void draw_editor(int col, int width, int rows, int split, const char *msg)
{
    char head[64];
    snprintf(head, sizeof head, "Code%s", !rom ? "" : split ? "  (Ctrl+T: ROM, Ctrl+E: hide)" :
                                                       "  (Ctrl+E: show the ROM)");
    sdk_title_bar(1, col, width, head, focus == FOCUS_EDITOR);

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
        sdk_put_row(2 + i, col, GUTTER, num, GUTTER);
        console_write("\x1b[0m");
        const char *s;
        int n = sdk_edit_line(l, &s);
        if (!s) {
            sdk_put_row(2 + i, col + GUTTER, tw, "", 0);
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
    const char *last = msg && msg[0] ? msg : sdk_note[0] && !(split && focus == FOCUS_ROM) ? sdk_note : st;
    sdk_put_row(rows, col, width, last, 200);
    console_write("\x1b[0m");
    cur_row = 1 + 1 + (line - top_line);                    /* where the text cursor is on the screen */
    cur_col = col + GUTTER + (c - left_col);
}

/* The lines of the two read-only views. */
static int asm_text_line(void *ctx, int line, char *out, size_t n)
{
    (void)ctx;
    return snprintf(out, n, "%s", line >= 0 && line < asm_lines ? asm_line[line] : "");
}

static int hex_text_line(void *ctx, int line, char *out, size_t n)
{
    (void)ctx;
    sdk_rom_hex_row(rom, rom_size, (size_t)line * (size_t)hex_bpr, hex_width, out, n);
    return (int)strlen(out);
}

/* Rows of a read-only view from the ANSI row first: the text, the selection, and where its cursor is. A label
 * or a comment line of the listing stands out. */
static void view_rows(sdk_view_t *v, sdk_view_line_t fn, int first, int col, int width, int h, int listing)
{
    sdk_view_scroll(v, h, width);
    int l1 = 0, c1 = 0, l2 = 0, c2 = 0;
    const int selected = sdk_view_selection(v, &l1, &c1, &l2, &c2);
    for (int i = 0; i < h; i++) {
        const int l = v->top + i;
        char s[SDK_VIEW_LINE];
        int n = l < v->lines ? fn(NULL, l, s, sizeof s) : 0;
        const int mark = listing && n && (s[n - 1] == ':' || s[0] == ';');
        int sc = 0, ec = 0;
        if (selected && l >= l1 && l <= l2) {
            sc = (l == l1 ? c1 : 0) - v->left;
            ec = (l == l2 ? c2 : n + 1) - v->left;
        }
        const char *from = s;
        if (v->left >= n) {
            from = "";
            n = 0;
        } else {
            from += v->left;
            n -= v->left;
        }
        console_write(mark ? "\x1b[93m" : "\x1b[0m");
        text_row(first + i, col, width, from, n, sc, ec);
    }
    rom_cur_row = first + (v->line - v->top);
    rom_cur_col = col + (v->col - v->left);
}

/* The ASM side: the rows 2 to rows-1 are the listing; for a ROM with banks row 2 names the bank. */
static void draw_listing(int col, int width, int rows)
{
    const int first = asm_info.banks ? 3 : 2;
    const int h = rows - first;
    asm_page = h > 1 ? h - 1 : 1;
    if (asm_info.banks) {
        char b[64];
        snprintf(b, sizeof b, "PRG bank %d/%d (%d KB) at $%04X%s", asm_info.bank, asm_info.banks - 1, asm_info.kb,
                 (unsigned)asm_info.addr, asm_info.guess ? " guess" : "");
        console_write("\x1b[96m");
        sdk_put_row(2, col, width, b, (int)strlen(b));
    }
    view_rows(&asm_view, asm_text_line, first, col, width, h, 1);
    char st[64];
    if (asm_info.banks)
        snprintf(st, sizeof st, "ASM %d/%d  +/-: bank  a: hex", asm_view.line + 1, asm_lines);
    else
        snprintf(st, sizeof st, "ASM  line %d of %d  a: hex", asm_view.line + 1, asm_lines);
    sdk_pane_status(col, width, rows, st, focus == FOCUS_ROM);
}

static void draw_rom(int col, int width, int rows)
{
    char head[80];
    snprintf(head, sizeof head, "ROM: %s", rom_name);
    sdk_title_bar(1, col, width, head, focus == FOCUS_ROM);
    if (asm_on) {
        draw_listing(col, width, rows);
        return;
    }
    if (tiles_on) {
        sdk_tiles_draw(col, width, rows, 1, focus == FOCUS_ROM);
        return;
    }

    int row = 2;
    console_write("\x1b[93m");
    if (rom_info.title[0]) {
        sdk_put_row(row++, col, width, rom_info.title, (int)strlen(rom_info.title));
    }
    for (int i = 0; i < rom_info.lines && row < rows; i++)
        sdk_put_row(row++, col, width, rom_info.line[i], (int)strlen(rom_info.line[i]));
    char sz[40];
    snprintf(sz, sizeof sz, "File %u KB", (unsigned)((rom_size + 1023) / 1024));
    if (row < rows)
        sdk_put_row(row++, col, width, sz, (int)strlen(sz));
    console_write("\x1b[0m");
    row++;

    const int h = rows - row;                       /* up to the row before the last */
    hex_bpr = sdk_rom_hex_bytes(width);
    hex_width = width;
    hex_page = h > 0 ? h : 1;
    hex_view.lines = (int)((rom_size + (size_t)hex_bpr - 1) / (size_t)hex_bpr);
    view_rows(&hex_view, hex_text_line, row, col, width, h, 0);

    /* the offset of the byte under the cursor: in the numbers or in the letters, else the start of the row */
    const int numbers = 7, letters = numbers + 3 * hex_bpr;
    int byte = 0;
    if (hex_view.col >= letters && hex_view.col < letters + hex_bpr)
        byte = hex_view.col - letters;
    else if (hex_view.col >= numbers && hex_view.col < letters)
        byte = (hex_view.col - numbers) / 3;
    size_t at_byte = (size_t)hex_view.line * (size_t)hex_bpr + (size_t)byte;
    if (at_byte >= rom_size && rom_size)
        at_byte = rom_size - 1;
    char st[48];
    snprintf(st, sizeof st, "Offset %06X  a: ASM  t: tiles", (unsigned)at_byte);
    sdk_pane_status(col, width, rows, st, focus == FOCUS_ROM);
}

void sdk_code_draw(int cols, int rows, const char *msg)
{
    const int split = rom != NULL && !rom_hidden && cols >= 40;
    if (!split)
        focus = FOCUS_EDITOR;
    const int lw = split ? cols / 2 : cols;         /* the left side: up to and with the divider */
    draw_editor(1, lw - 1, rows, split, msg);
    if (!split) {
        sdk_at(cur_row, cur_col);
        console_cursor_mode(CONSOLE_CURSOR_BLOCK);
        return;
    }
    console_write("\x1b[90m");
    for (int r = 1; r <= rows; r++) {
        sdk_at(r, lw);
        console_putc((char)0xB3);
    }
    console_write("\x1b[0m");
    draw_rom(lw + 1, cols - lw - 1, rows);
    if (focus == FOCUS_EDITOR) {
        sdk_at(cur_row, cur_col);                               /* a solid square, the letter under it swapped */
        console_cursor_mode(CONSOLE_CURSOR_BLOCK);
    } else if (!tiles_on) {                                 /* the same square in the dump and the listing */
        sdk_at(rom_cur_row, rom_cur_col);
        console_cursor_mode(CONSOLE_CURSOR_BLOCK);
    } else {
        sdk_at(1, 1);
        console_cursor_mode(CONSOLE_CURSOR_HIDDEN);
    }
    if (tiles_on)
        sdk_tiles_paint();                                  /* last: the console repaints the cells it writes */
}

/* ---- keys ---- */

/* The keys of the dump and of the listing: the moves of the editor, with Shift to select; Ctrl+C copies into
 * the clipboard of the editor. Nothing changes the ROM. */
static int view_key(sdk_view_t *v, sdk_view_line_t fn, int page, int key, int mods)
{
    const int ctrl = (mods & SDK_MOD_CTRL) != 0, alt = (mods & SDK_MOD_ALT) != 0;
    int m = -1;
    switch (key) {
    case HID_KEY_UP: m = ctrl && alt ? SDK_VIEW_DOC_START : alt ? SDK_VIEW_PGUP : SDK_VIEW_UP; break;
    case HID_KEY_DOWN: m = ctrl && alt ? SDK_VIEW_DOC_END : alt ? SDK_VIEW_PGDN : SDK_VIEW_DOWN; break;
    case HID_KEY_LEFT: m = ctrl && alt ? SDK_VIEW_HOME : ctrl ? SDK_VIEW_WORD_LEFT : SDK_VIEW_LEFT; break;
    case HID_KEY_RIGHT: m = ctrl && alt ? SDK_VIEW_END : ctrl ? SDK_VIEW_WORD_RIGHT : SDK_VIEW_RIGHT; break;
    case HID_KEY_HOME: m = SDK_VIEW_HOME; break;
    case HID_KEY_END: m = SDK_VIEW_END; break;
    case HID_KEY_PGUP: m = SDK_VIEW_PGUP; break;
    case HID_KEY_PGDN: m = SDK_VIEW_PGDN; break;
    case 0x01: sdk_view_select_all(v, fn, NULL); return 1;          /* Ctrl+A */
    case 0x03: {                                                    /* Ctrl+C */
        char *text = malloc(SDK_EDIT_MAX);
        if (!text) {
            snprintf(sdk_note, sizeof sdk_note, "Not enough memory to copy");
            return 1;
        }
        int cut = 0;
        const size_t n = sdk_view_copy(v, fn, NULL, text, SDK_EDIT_MAX, &cut);
        sdk_edit_clip_set(text, n);
        free(text);
        snprintf(sdk_note, sizeof sdk_note, cut ? "Copied %u bytes, the most it holds" : "Copied %u bytes", (unsigned)n);
        return 1;
    }
    case 0x18:                                                      /* Ctrl+X */
    case 0x16:                                                      /* Ctrl+V */
        snprintf(sdk_note, sizeof sdk_note, "The ROM is read only: Ctrl+C copies");
        return 1;
    default: return 0;
    }
    sdk_view_move(v, m, mods & SDK_MOD_SHIFT, page, fn, NULL);
    return 1;
}

static int listing_key(int key, int mods)
{
    if (asm_info.banks > 1 && (key == '+' || key == '=' || key == '-')) {          /* the bank after, before */
        const int step = key == '-' ? asm_info.banks - 1 : 1;
        char err[sizeof sdk_note];
        if (make_listing((asm_info.bank + step) % asm_info.banks, err, sizeof err) != 0)
            snprintf(sdk_note, sizeof sdk_note, "%s", err);
        return 1;
    }
    return view_key(&asm_view, asm_text_line, asm_page, key, mods);
}

static int rom_key(int key, int mods)
{
    if (key == 't') {                           /* hex or tiles */
        tiles_on = !tiles_on;
        asm_on = 0;
        return 1;
    }
    if (key == 'a') {                           /* hex or ASM */
        char err[sizeof sdk_note];
        if (!asm_on && !asm_text && make_listing(-1, err, sizeof err) != 0)
            snprintf(sdk_note, sizeof sdk_note, "%s", err);
        else {
            asm_on = !asm_on;
            tiles_on = 0;
        }
        return 1;
    }
    if (asm_on)
        return listing_key(key, mods);
    if (tiles_on)
        return sdk_tiles_key(key, mods);
    return view_key(&hex_view, hex_text_line, hex_page, key, mods);
}

/* A move of the cursor: Shift keeps the selection (and grows it), no Shift ends it. */
static void move(int mods)
{
    sdk_edit_anchor(mods & SDK_MOD_SHIFT ? 1 : 0);
}

static int editor_key(int key, int mods)
{
    const int ctrl = (mods & SDK_MOD_CTRL) != 0, alt = (mods & SDK_MOD_ALT) != 0;
    sdk_note[0] = 0;
    switch (key) {
    case 0x01: sdk_edit_select_all(); return 1;                  /* Ctrl+A */
    case 0x03:                                                    /* Ctrl+C: copy the selection or the line */
    case 0x18: {                                                  /* Ctrl+X: cut it */
        const int sel = sdk_edit_copy(key == 0x18) > 0;
        if (sel)
            snprintf(sdk_note, sizeof sdk_note, "%s %u bytes", key == 0x18 ? "Cut" : "Copied", (unsigned)sdk_edit_clip_length());
        return 1;
    }
    case 0x16:                                                    /* Ctrl+V: paste */
        snprintf(sdk_note, sizeof sdk_note, "Pasted %d bytes", sdk_edit_paste());
        return 1;
    case 0x04: sdk_edit_duplicate_line(); return 1;              /* Ctrl+D: duplicate the line */
    case 0x1A:                                                    /* Ctrl+Z: undo; with Shift: redo */
    case 0x19:                                                    /* Ctrl+Y: redo */
        if (key == 0x1A && !(mods & SDK_MOD_SHIFT))
            snprintf(sdk_note, sizeof sdk_note, "%s", sdk_edit_undo() ? "Undo" : "Nothing to undo");
        else
            snprintf(sdk_note, sizeof sdk_note, "%s", sdk_edit_redo() ? "Redo" : "Nothing to redo");
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
    sdk_note[0] = 0;
    const int swap = key == 0x14 || (key == 9 && (mods & SDK_MOD_CTRL));    /* Ctrl+T, Ctrl+Tab */
    if (swap || key == 0x17 || key == 0x05) {                  /* Ctrl+W: close the ROM; Ctrl+E: hide it */
        if (!rom)
            snprintf(sdk_note, sizeof sdk_note, "No ROM open (Esc > Open ROM)");
        else if (swap) {
            focus = rom_hidden || focus == FOCUS_EDITOR ? FOCUS_ROM : FOCUS_EDITOR;
            rom_hidden = 0;
        } else if (key == 0x05) {
            rom_hidden = !rom_hidden;
            focus = FOCUS_EDITOR;
        } else {
            close_rom();
            snprintf(sdk_note, sizeof sdk_note, "ROM closed");
        }
        return 1;
    }
    if (rom && focus == FOCUS_ROM)
        return rom_key(key, mods);
    return editor_key(key, mods);
}
