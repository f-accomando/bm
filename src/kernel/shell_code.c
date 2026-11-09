/*
 * Shell Code: Interactive Editor, Compiler, and Runner for bm shell.
 *
 * Designed for Raspberry Pi Zero W & PowKiddy RGB30:
 * - Direct, responsive typing (no complex vim colon commands)
 * - Esc opens a clean centered popup menu with all actions (Save, Run, Disasm, New, Exit)
 * - Row 0 displays the Tab Bar, replacing the kernel version status bar
 * - High-contrast block cursor with inverted character (zero flicker, zero drift)
 * - Zero-scroll rendering: exact ANSI row positioning with no screen wrapping
 * - Automatic saving to /code-b/ for all .b files
 * - Compiler error feedback jumping straight to the error line
 * - Native ARMv6 runner with microsecond profiling and framebuffer graphics
 * - ARMv6 A32 / BMN1 disassembly viewer
 */
#include "shell_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bm/bmlc.h"
#include "bm/bmnative.h"

#ifndef SHELL_CODE_HOST_TEST
#include "gfx/console.h"
#include "drivers/fb.h"
#include "drivers/timer.h"
#include "input.h"
#include "usb/hid.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "monitor_fs.h"
#else
#ifndef HID_KEY_UP
#define HID_KEY_UP      0xF0
#define HID_KEY_DOWN    0xF1
#define HID_KEY_LEFT    0xF2
#define HID_KEY_RIGHT   0xF3
#define HID_KEY_HOME    0xF4
#define HID_KEY_END     0xF5
#define HID_KEY_PGUP    0xF6
#define HID_KEY_PGDN    0xF7
#define HID_KEY_DEL     0xF8
#define HID_KEY_F1      0xF9
#define HID_KEY_F6      0xE6
#endif
#endif

#define SC_KEY_F5       (HID_KEY_F1 + 4)
#define SC_KEY_F7       (HID_KEY_F6 + 1)

#define SC_MAX_TABS     8
#define SC_PATH_MAX     128
#define SC_LINE_INIT    64

enum {
    SC_SYN_PLAIN = 0,
    SC_SYN_B     = 1,
    SC_SYN_LUA   = 2,
    SC_SYN_C     = 3
};

typedef struct {
    char *chars;
    int   len;
    int   cap;
} sc_line_t;

typedef struct {
    char       path[SC_PATH_MAX];
    char       name[32];
    sc_line_t *lines;
    int        nlines;
    int        lines_cap;
    int        cx;         /* cursor column (0-based) */
    int        cy;         /* cursor row (0-based) */
    int        row_off;    /* vertical scroll offset */
    int        col_off;    /* horizontal scroll offset */
    int        syntax;     /* SC_SYN_* */
    int        dirty;      /* 1 if modified */
    int        err_line;   /* 1-based error line from compiler */
    char       status[256];
} sc_tab_t;

enum {
    MENU_RESUME = 0,
    MENU_SAVE,
    MENU_SAVE_AS,
    MENU_RUN,
    MENU_DISASM,
    MENU_NEW,
    MENU_NEXT_TAB,
    MENU_CLOSE_TAB,
    MENU_EXIT,
    MENU_COUNT
};

static const char *const g_menu_labels[MENU_COUNT] = {
    "Riprendi editing",
    "Salva            (Ctrl+S)",
    "Salva con nome...",
    "Esegui .b        (Ctrl+R)",
    "Disassembla .b   (Ctrl+D)",
    "Nuovo file       (Ctrl+N)",
    "Prossimo Tab     (Ctrl+T)",
    "Chiudi Tab",
    "Esci dall'app"
};

typedef struct {
    sc_tab_t tabs[SC_MAX_TABS];
    int      ntabs;
    int      cur_tab;
    int      show_menu;
    int      menu_sel;
    int      quit_requested;
    int      screen_cols;
    int      screen_rows;
} sc_state_t;

static sc_state_t g_sc;

/* ---- Path Normalization (/code-b/) ---- */

static void sc_normalize_path(char *dest, size_t dest_cap, const char *src)
{
    if (!src || !*src) {
        snprintf(dest, dest_cap, "/code-b/scratch.b");
        return;
    }
    const char *ext = strrchr(src, '.');
    int is_b = (ext && strcmp(ext, ".b") == 0);

    if (is_b) {
        if (src[0] == '/') {
            snprintf(dest, dest_cap, "%s", src);
        } else {
            snprintf(dest, dest_cap, "/code-b/%s", src);
        }
    } else {
        if (src[0] == '/') {
            snprintf(dest, dest_cap, "%s", src);
        } else {
            snprintf(dest, dest_cap, "/%s", src);
        }
    }
}

/* ---- Line Operations ---- */

static void sc_line_init(sc_line_t *l)
{
    l->chars = malloc(SC_LINE_INIT);
    l->len = 0;
    l->cap = SC_LINE_INIT;
    if (l->chars)
        l->chars[0] = '\0';
}

static void sc_line_free(sc_line_t *l)
{
    free(l->chars);
    l->chars = NULL;
    l->len = 0;
    l->cap = 0;
}

static void sc_line_ensure_cap(sc_line_t *l, int needed)
{
    if (needed + 1 <= l->cap)
        return;
    int ncap = l->cap ? l->cap * 2 : SC_LINE_INIT;
    while (ncap < needed + 1)
        ncap *= 2;
    char *p = realloc(l->chars, (size_t)ncap);
    if (p) {
        l->chars = p;
        l->cap = ncap;
    }
}

static void sc_line_insert_char(sc_line_t *l, int at, char c)
{
    if (at < 0) at = 0;
    if (at > l->len) at = l->len;
    sc_line_ensure_cap(l, l->len + 1);
    if (!l->chars) return;
    memmove(&l->chars[at + 1], &l->chars[at], (size_t)(l->len - at + 1));
    l->chars[at] = c;
    l->len++;
}

static void sc_line_delete_char(sc_line_t *l, int at)
{
    if (at < 0 || at >= l->len) return;
    memmove(&l->chars[at], &l->chars[at + 1], (size_t)(l->len - at));
    l->len--;
}

static void sc_line_append(sc_line_t *l, const char *s, int len)
{
    if (len <= 0) return;
    sc_line_ensure_cap(l, l->len + len);
    if (!l->chars) return;
    memcpy(&l->chars[l->len], s, (size_t)len);
    l->len += len;
    l->chars[l->len] = '\0';
}

/* ---- Tab and Buffer Operations ---- */

static void sc_tab_detect_syntax(sc_tab_t *t)
{
    const char *dot = strrchr(t->path, '.');
    if (!dot) {
        t->syntax = SC_SYN_PLAIN;
        return;
    }
    if (!strcmp(dot, ".b"))
        t->syntax = SC_SYN_B;
    else if (!strcmp(dot, ".lua"))
        t->syntax = SC_SYN_LUA;
    else if (!strcmp(dot, ".c") || !strcmp(dot, ".h"))
        t->syntax = SC_SYN_C;
    else
        t->syntax = SC_SYN_PLAIN;
}

static void sc_tab_update_name(sc_tab_t *t)
{
    const char *slash = strrchr(t->path, '/');
    const char *n = slash ? slash + 1 : t->path;
    if (!*n) n = "scratch.b";
    snprintf(t->name, sizeof(t->name), "%.31s", n);
}

static void sc_tab_ensure_lines_cap(sc_tab_t *t, int needed)
{
    if (needed <= t->lines_cap)
        return;
    int ncap = t->lines_cap ? t->lines_cap * 2 : 16;
    while (ncap < needed)
        ncap *= 2;
    sc_line_t *p = realloc(t->lines, (size_t)ncap * sizeof(sc_line_t));
    if (p) {
        t->lines = p;
        t->lines_cap = ncap;
    }
}

static void sc_tab_insert_line(sc_tab_t *t, int at, const char *s, int len)
{
    if (at < 0) at = 0;
    if (at > t->nlines) at = t->nlines;
    sc_tab_ensure_lines_cap(t, t->nlines + 1);
    if (!t->lines) return;
    if (at < t->nlines) {
        memmove(&t->lines[at + 1], &t->lines[at], (size_t)(t->nlines - at) * sizeof(sc_line_t));
    }
    sc_line_init(&t->lines[at]);
    if (len > 0 && s)
        sc_line_append(&t->lines[at], s, len);
    t->nlines++;
    t->dirty = 1;
}

static void sc_tab_delete_line(sc_tab_t *t, int at)
{
    if (at < 0 || at >= t->nlines) return;
    sc_line_free(&t->lines[at]);
    if (at + 1 < t->nlines) {
        memmove(&t->lines[at], &t->lines[at + 1], (size_t)(t->nlines - at - 1) * sizeof(sc_line_t));
    }
    t->nlines--;
    if (t->nlines == 0) {
        sc_tab_insert_line(t, 0, "", 0);
    }
    t->dirty = 1;
}

static void sc_tab_init_empty(sc_tab_t *t, const char *path)
{
    memset(t, 0, sizeof(*t));
    sc_normalize_path(t->path, sizeof(t->path), path);
    sc_tab_update_name(t);
    sc_tab_detect_syntax(t);
    sc_tab_insert_line(t, 0, "", 0);
    t->dirty = 0;
}

static void sc_tab_free(sc_tab_t *t)
{
    for (int i = 0; i < t->nlines; i++) {
        sc_line_free(&t->lines[i]);
    }
    free(t->lines);
    t->lines = NULL;
    t->nlines = 0;
    t->lines_cap = 0;
}

static char *sc_tab_get_text(const sc_tab_t *t, size_t *out_len)
{
    size_t total = 0;
    for (int i = 0; i < t->nlines; i++) {
        total += (size_t)t->lines[i].len + 1;
    }
    char *buf = malloc(total + 1);
    if (!buf) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    size_t pos = 0;
    for (int i = 0; i < t->nlines; i++) {
        if (t->lines[i].len > 0) {
            memcpy(buf + pos, t->lines[i].chars, (size_t)t->lines[i].len);
            pos += (size_t)t->lines[i].len;
        }
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    if (out_len) *out_len = pos;
    return buf;
}

static void sc_tab_set_text(sc_tab_t *t, const char *text, size_t len)
{
    for (int i = 0; i < t->nlines; i++)
        sc_line_free(&t->lines[i]);
    t->nlines = 0;

    const char *p = text;
    const char *end = text + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (line_len > 0 && p[line_len - 1] == '\r')
            line_len--;
        sc_tab_insert_line(t, t->nlines, p, (int)line_len);
        p += nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
    }
    if (t->nlines == 0)
        sc_tab_insert_line(t, 0, "", 0);
    t->cx = 0;
    t->cy = 0;
    t->row_off = 0;
    t->col_off = 0;
    t->dirty = 0;
}

/* ---- File I/O ---- */

static int sc_tab_load(sc_tab_t *t, const char *path)
{
    sc_tab_init_empty(t, path);
#ifndef SHELL_CODE_HOST_TEST
    fat_entry_t e;
    if (fat_find(t->path, &e) == 0 && !e.is_dir) {
        uint8_t *data = NULL;
        size_t len = 0;
        if (fat_load(&e, &data, &len) == 0) {
            sc_tab_set_text(t, (const char *)data, len);
            free(data);
            t->dirty = 0;
            snprintf(t->status, sizeof(t->status), "Caricato %s (%zu bytes)", t->path, len);
            return 0;
        }
    }
#endif
    snprintf(t->status, sizeof(t->status), "Nuovo file: %s", t->path);
    return 0;
}

static int sc_tab_save(sc_tab_t *t, const char *override_path)
{
    if (override_path && *override_path) {
        sc_normalize_path(t->path, sizeof(t->path), override_path);
        sc_tab_update_name(t);
        sc_tab_detect_syntax(t);
    }

    size_t len = 0;
    char *text = sc_tab_get_text(t, &len);
    if (!text) {
        snprintf(t->status, sizeof(t->status), "Errore: memoria esaurita");
        return -1;
    }

#ifndef SHELL_CODE_HOST_TEST
    if (strncmp(t->path, "/code-b/", 8) == 0) {
        fat_mkdirs("/code-b");
    }

    fat_entry_t e;
    int ok = 0;
    if (fat_find(t->path, &e) == 0 && !e.is_dir) {
        ok = (fat_replace(t->path, text, len) == 0);
    } else {
        char dir[128];
        snprintf(dir, sizeof(dir), "%s", t->path);
        char *slash = strrchr(dir, '/');
        const char *name = t->path;
        if (slash) {
            name = slash + 1;
            if (slash == dir)
                slash[1] = '\0';
            else
                *slash = '\0';
            fat_mkdirs(dir);
            ok = (fat_write_file(dir, name, text, len) == 0);
        } else {
            ok = (fat_write_file("/", name, text, len) == 0);
        }
    }
    free(text);
    if (ok) {
        t->dirty = 0;
        snprintf(t->status, sizeof(t->status), "Salvato %s (%zu bytes)", t->path, len);
        return 0;
    } else {
        snprintf(t->status, sizeof(t->status), "Errore salvataggio: %s", fat_error());
        return -1;
    }
#else
    free(text);
    t->dirty = 0;
    snprintf(t->status, sizeof(t->status), "Salvato %s (%zu bytes)", t->path, len);
    return 0;
#endif
}

/* ---- Syntax Highlighting ---- */

static int is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_part(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int sc_match_kw(const char *tok, int len, const char *const *list, int count)
{
    for (int i = 0; i < count; i++) {
        if ((int)strlen(list[i]) == len && strncmp(tok, list[i], (size_t)len) == 0)
            return 1;
    }
    return 0;
}

static const char *const b_kw[] = {
    "function", "end", "local", "if", "then", "else", "elseif",
    "while", "do", "for", "return", "break", "struct"
};
static const char *const b_types[] = {
    "i32", "f32", "bool"
};
static const char *const b_builtins[] = {
    "cls", "pix", "rectfill", "spr", "btn", "abs", "min", "max", "sqrt", "floor"
};

static const char *const lua_kw[] = {
    "function", "end", "local", "if", "then", "else", "elseif",
    "while", "do", "for", "in", "return", "break", "nil", "and", "or", "not"
};

static const char *const c_kw[] = {
    "int", "void", "char", "float", "double", "static", "const", "return",
    "if", "else", "while", "for", "do", "struct", "typedef", "enum", "switch",
    "case", "break", "continue", "sizeof", "uint32_t", "int32_t", "uint8_t"
};

/* Formats line content with syntax highlighting and explicit inverse block cursor */
static void sc_format_line(const sc_tab_t *t, int file_line, int is_cursor_line,
                           int cursor_cx, int col_off, int max_cols, char *out, size_t max_len)
{
    if (file_line < 0 || file_line >= t->nlines) {
        out[0] = '\0';
        return;
    }
    const sc_line_t *l = &t->lines[file_line];
    const char *s = l->chars;
    int len = l->len;
    size_t pos = 0;

    int col = col_off;
    int end_col = col_off + max_cols;

    while (col < end_col) {
        if (is_cursor_line && col == cursor_cx) {
            /* Render active block cursor: reverse video / negative */
            char c = (col < len) ? s[col] : ' ';
            pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m", c);
            col++;
            continue;
        }

        if (col >= len) {
            /* Past end of text: fill with spaces */
            if (pos + 1 < max_len)
                out[pos++] = ' ';
            col++;
            continue;
        }

        /* Comments */
        if ((t->syntax == SC_SYN_B || t->syntax == SC_SYN_LUA) && col + 1 < len && s[col] == '-' && s[col + 1] == '-') {
            pos += snprintf(out + pos, max_len - pos, "\x1b[90m");
            while (col < end_col && col < len) {
                if (is_cursor_line && col == cursor_cx) {
                    pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m\x1b[90m", s[col]);
                } else if (pos + 1 < max_len) {
                    out[pos++] = s[col];
                }
                col++;
            }
            pos += snprintf(out + pos, max_len - pos, "\x1b[0m");
            continue;
        }
        if (t->syntax == SC_SYN_C && col + 1 < len && s[col] == '/' && s[col + 1] == '/') {
            pos += snprintf(out + pos, max_len - pos, "\x1b[90m");
            while (col < end_col && col < len) {
                if (is_cursor_line && col == cursor_cx) {
                    pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m\x1b[90m", s[col]);
                } else if (pos + 1 < max_len) {
                    out[pos++] = s[col];
                }
                col++;
            }
            pos += snprintf(out + pos, max_len - pos, "\x1b[0m");
            continue;
        }

        /* Strings */
        if (s[col] == '"' || (t->syntax == SC_SYN_LUA && s[col] == '\'')) {
            char quote = s[col];
            pos += snprintf(out + pos, max_len - pos, "\x1b[33m");
            int inside = 1;
            while (col < end_col && col < len && inside) {
                if (is_cursor_line && col == cursor_cx) {
                    pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m\x1b[33m", s[col]);
                } else if (pos + 1 < max_len) {
                    out[pos++] = s[col];
                }
                if (col > 0 && s[col] == quote && s[col - 1] != '\\')
                    inside = 0;
                col++;
            }
            pos += snprintf(out + pos, max_len - pos, "\x1b[0m");
            continue;
        }

        /* Identifiers and Keywords */
        if (is_ident_start(s[col])) {
            int start = col;
            while (col < len && is_ident_part(s[col]))
                col++;
            int tok_len = col - start;
            const char *color = NULL;

            if (t->syntax == SC_SYN_B) {
                if (sc_match_kw(s + start, tok_len, b_kw, sizeof(b_kw)/sizeof(b_kw[0])))
                    color = "\x1b[1;93m";       /* bold yellow */
                else if (sc_match_kw(s + start, tok_len, b_types, sizeof(b_types)/sizeof(b_types[0])))
                    color = "\x1b[1;92m";       /* bright green */
                else if (sc_match_kw(s + start, tok_len, b_builtins, sizeof(b_builtins)/sizeof(b_builtins[0])))
                    color = "\x1b[1;96m";       /* cyan */
                else if ((tok_len == 4 && strncmp(s + start, "true", 4) == 0) ||
                         (tok_len == 5 && strncmp(s + start, "false", 5) == 0))
                    color = "\x1b[35m";         /* magenta */
            } else if (t->syntax == SC_SYN_LUA) {
                if (sc_match_kw(s + start, tok_len, lua_kw, sizeof(lua_kw)/sizeof(lua_kw[0])))
                    color = "\x1b[1;93m";
            } else if (t->syntax == SC_SYN_C) {
                if (sc_match_kw(s + start, tok_len, c_kw, sizeof(c_kw)/sizeof(c_kw[0])))
                    color = "\x1b[1;96m";
            }

            for (int k = start; k < col && k < end_col; k++) {
                if (is_cursor_line && k == cursor_cx) {
                    pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m", s[k]);
                } else {
                    if (color && k == start)
                        pos += snprintf(out + pos, max_len - pos, "%s", color);
                    if (pos + 1 < max_len)
                        out[pos++] = s[k];
                }
            }
            if (color)
                pos += snprintf(out + pos, max_len - pos, "\x1b[0m");
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)s[col])) {
            int start = col;
            while (col < len && (isalnum((unsigned char)s[col]) || s[col] == '.'))
                col++;
            pos += snprintf(out + pos, max_len - pos, "\x1b[35m");
            for (int k = start; k < col && k < end_col; k++) {
                if (is_cursor_line && k == cursor_cx) {
                    pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m\x1b[35m", s[k]);
                } else if (pos + 1 < max_len) {
                    out[pos++] = s[k];
                }
            }
            pos += snprintf(out + pos, max_len - pos, "\x1b[0m");
            continue;
        }

        /* Default plain character */
        if (pos + 1 < max_len)
            out[pos++] = s[col];
        col++;
    }
    out[pos] = '\0';
}

/* ---- Screen Drawing (Zero-Scroll, Zero-Flicker) ---- */

static void sc_draw_tabs_status(void)
{
    char left[128];
    size_t pos = 0;
    for (int i = 0; i < g_sc.ntabs; i++) {
        sc_tab_t *t = &g_sc.tabs[i];
        if (i == g_sc.cur_tab) {
            pos += snprintf(left + pos, sizeof(left) - pos, "[%d: %s%s] ",
                            i + 1, t->name, t->dirty ? "*" : "");
        } else {
            pos += snprintf(left + pos, sizeof(left) - pos, " %d: %s%s  ",
                            i + 1, t->name, t->dirty ? "*" : "");
        }
    }
#ifndef SHELL_CODE_HOST_TEST
    sc_tab_t *cur = &g_sc.tabs[g_sc.cur_tab];
    if (cur->err_line > 0 && cur->status[0]) {
        console_set_status(left, cur->status);
    } else {
        console_set_status(left, "[Esc] Menu");
    }
#endif
}

static void sc_draw_editor(void)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];
    int rows_avail = g_sc.screen_rows - 1;   /* row 0 is tab status, rows 1..rows-1 are editor lines */
    if (rows_avail < 1) rows_avail = 1;
    int cols = g_sc.screen_cols;

    int gw = (t->nlines >= 1000) ? 6 : 5;
    int text_cols = cols - gw - 1;
    if (text_cols < 10) text_cols = 10;

    char line_buf[512];
    char text_buf[512];

    for (int r = 0; r < rows_avail; r++) {
        int file_line = t->row_off + r;
        int ansi_row = 1 + r;   /* 1-based row below status row 0 */

        int written = snprintf(line_buf, sizeof(line_buf), "\x1b[%d;1H", ansi_row);

        if (file_line < t->nlines) {
            int is_err = (file_line + 1 == t->err_line);
            int is_cur = (file_line == t->cy);
            if (is_err) {
                written += snprintf(line_buf + written, sizeof(line_buf) - (size_t)written,
                                    "\x1b[1;91m%*d!\x1b[0m", gw - 1, file_line + 1);
            } else {
                written += snprintf(line_buf + written, sizeof(line_buf) - (size_t)written,
                                    "\x1b[90m%*d\xb3\x1b[0m", gw - 1, file_line + 1);
            }
            sc_format_line(t, file_line, is_cur, t->cx, t->col_off, text_cols, text_buf, sizeof(text_buf));
            snprintf(line_buf + written, sizeof(line_buf) - (size_t)written, "%s\x1b[K", text_buf);
        } else {
            snprintf(line_buf + written, sizeof(line_buf) - (size_t)written,
                     "\x1b[94m~\x1b[0m%*s\x1b[90m\xb3\x1b[0m\x1b[K", gw - 2, "");
        }
#ifndef SHELL_CODE_HOST_TEST
        console_write(line_buf);
#endif
    }
}

static void sc_draw_popup_menu(void)
{
    int w = 36;
    int h = MENU_COUNT + 4;
    int top = (g_sc.screen_rows - h) / 2;
    if (top < 1) top = 1;
    int left = (g_sc.screen_cols - w) / 2;
    if (left < 1) left = 1;

    char buf[256];

    /* Header */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xda\xc4\xc4 Shell Code Menu \xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xbf\x1b[0m",
             top, left);
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3%*s\xb3\x1b[0m", top + 1, left, w - 2, "");
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif

    /* Menu Items */
    for (int i = 0; i < MENU_COUNT; i++) {
        int r = top + 2 + i;
        if (i == g_sc.menu_sel) {
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3\x1b[1;30;47m > %-28s < \x1b[1;97;44m\xb3\x1b[0m",
                     r, left, g_menu_labels[i]);
        } else {
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3   %-28s   \xb3\x1b[0m",
                     r, left, g_menu_labels[i]);
        }
#ifndef SHELL_CODE_HOST_TEST
        console_write(buf);
#endif
    }

    /* Footer */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3%*s\xb3\x1b[0m", top + 2 + MENU_COUNT, left, w - 2, "");
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xc0\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xc4\xd9\x1b[0m",
             top + 3 + MENU_COUNT, left);
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif
}

static void sc_scroll_to_cursor(sc_tab_t *t)
{
    int rows_avail = g_sc.screen_rows - 1;
    if (rows_avail < 1) rows_avail = 1;
    int gw = (t->nlines >= 1000) ? 6 : 5;
    int text_cols = g_sc.screen_cols - gw - 1;
    if (text_cols < 10) text_cols = 10;

    if (t->cy < t->row_off)
        t->row_off = t->cy;
    if (t->cy >= t->row_off + rows_avail)
        t->row_off = t->cy - rows_avail + 1;

    if (t->cx < t->col_off)
        t->col_off = t->cx;
    if (t->cx >= t->col_off + text_cols)
        t->col_off = t->cx - text_cols + 1;
}

/* ---- Compiler and Runner ---- */

static int sc_compile_b(sc_tab_t *t, uint8_t **out_blob, size_t *out_len)
{
    *out_blob = NULL;
    *out_len = 0;
    size_t src_len = 0;
    char *src = sc_tab_get_text(t, &src_len);
    if (!src) {
        snprintf(t->status, sizeof(t->status), "Errore memoria");
        return -1;
    }

    char err[256];
    err[0] = '\0';
    int rc = bml_compile_native(src, src_len, out_blob, out_len, err, sizeof(err));
    free(src);

    if (rc != 0) {
        t->err_line = 0;
        if (strncmp(err, "line ", 5) == 0) {
            t->err_line = atoi(err + 5);
            if (t->err_line > 0 && t->err_line <= t->nlines) {
                t->cy = t->err_line - 1;
                t->cx = 0;
                sc_scroll_to_cursor(t);
            }
        }
        snprintf(t->status, sizeof(t->status), "Errore sintassi: %s", err);
        return -1;
    }

    t->err_line = 0;
    snprintf(t->status, sizeof(t->status), "Compilato OK: %zu bytes BMN1 ARMv6", *out_len);
    return 0;
}

#ifndef SHELL_CODE_HOST_TEST
static int g_shell_did_draw_gfx = 0;

static int32_t shell_code_host_dispatch(int32_t host_id, const int32_t *args, int32_t nargs)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb) return 0;

    switch (host_id) {
    case BMN_HOST_CLS: {
        uint32_t col = (nargs > 0) ? (uint32_t)args[0] : 0;
        fb_fill_rect(fb, 0, 0, fb->width, fb->height, col);
        g_shell_did_draw_gfx = 1;
        return 0;
    }
    case BMN_HOST_PIX: {
        if (nargs >= 3) {
            uint32_t x = (uint32_t)args[0];
            uint32_t y = (uint32_t)args[1];
            uint32_t col = (uint32_t)args[2];
            if (x < fb->width && y < fb->height)
                fb_putpixel(fb, x, y, col);
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_RECTFILL: {
        if (nargs >= 5) {
            fb_fill_rect(fb, (uint32_t)args[0], (uint32_t)args[1],
                         (uint32_t)args[2], (uint32_t)args[3], (uint32_t)args[4]);
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_SPR: {
        if (nargs >= 3) {
            fb_fill_rect(fb, (uint32_t)args[1], (uint32_t)args[2], 8, 8, 0x00FF00FF);
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_BTN: {
        uint32_t b = (nargs > 0) ? (uint32_t)args[0] : 0;
        uint32_t btns = hid_buttons();
        return (btns & (1u << b)) ? 1 : 0;
    }
    default:
        break;
    }
    return 0;
}
#endif

static int sc_run_b(sc_tab_t *t, const char *arg_str)
{
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    if (sc_compile_b(t, &blob, &blob_len) != 0)
        return -1;

#ifndef SHELL_CODE_HOST_TEST
    bmnative_mod *m = NULL;
    const char *why = "";
    if (bmnative_load(&m, blob, blob_len, &why) != BMN_OK) {
        free(blob);
        snprintf(t->status, sizeof(t->status), "Caricamento fallito: %s", why);
        return -1;
    }

    int fn_idx = -1;
    if (t->cy < t->nlines) {
        const char *line = t->lines[t->cy].chars;
        const char *p = strstr(line, "function ");
        if (p) {
            char fname[32];
            p += 9;
            while (*p == ' ') p++;
            int k = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_') && k < 31)
                fname[k++] = *p++;
            fname[k] = '\0';
            fn_idx = bmnative_find(m, fname);
        }
    }
    if (fn_idx < 0) fn_idx = bmnative_find(m, "main");
    if (fn_idx < 0) fn_idx = bmnative_find(m, "run");
    if (fn_idx < 0) fn_idx = 0;

    const bmnative_fn *f = &m->fn[fn_idx];
    int32_t args[BMN_MAX_ARGS];
    memset(args, 0, sizeof(args));
    int nargs = f->nparams;

    if (nargs > 0 && arg_str && *arg_str) {
        args[0] = (int32_t)atoi(arg_str);
    } else if (nargs > 0) {
        args[0] = 10;
    }

    g_shell_did_draw_gfx = 0;
    bmnative_host_fn prev_host = bmnative_get_host_fn();
    bmnative_set_host_fn(shell_code_host_dispatch);

    uint32_t t0 = timer_ticks();
    int32_t ret = 0;
    int err = bmnative_call(m, fn_idx, args, nargs, &ret);
    uint32_t t1 = timer_ticks();
    uint32_t us = t1 - t0;

    bmnative_set_host_fn(prev_host);

    if (g_shell_did_draw_gfx) {
        kprintf("\n\x1b[1;92m-- Grafica disegnata. %s() ha restituito %ld (%lu us). Premi un tasto --\x1b[0m\n",
                f->name, (long)ret, (unsigned long)us);
        input_getc();
        console_clear();
    }

    if (err == BMN_OK) {
        snprintf(t->status, sizeof(t->status), "%s() = %ld (0x%lX) [%lu us]",
                 f->name, (long)ret, (unsigned long)ret, (unsigned long)us);
    } else {
        snprintf(t->status, sizeof(t->status), "Trap runtime (%d) in %s", err, f->name);
    }

    bmnative_free(m);
    free(blob);
    return (err == BMN_OK) ? 0 : -1;
#else
    (void)arg_str;
    free(blob);
    snprintf(t->status, sizeof(t->status), "Compilato OK (runner simulato)");
    return 0;
#endif
}

/* ---- Disassembly Viewer ---- */

static void sc_disasm_armv6_word(uint32_t w, char *buf, size_t cap)
{
    uint32_t cond = (w >> 28) & 0xFu;
    static const char *const conds[] = {
        "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
        "hi", "ls", "ge", "lt", "gt", "le", "", "nv"
    };
    const char *cc = conds[cond];

    if (((w >> 25) & 7u) == 5u) {
        int is_bl = (w >> 24) & 1u;
        int32_t imm24 = (int32_t)(w & 0xFFFFFFu);
        if (imm24 & 0x800000) imm24 -= 0x1000000;
        int32_t byte_off = (imm24 + 2) * 4;
        snprintf(buf, cap, "%s%s %+d", is_bl ? "bl" : "b", cc, (int)byte_off);
        return;
    }
    if ((w & 0x0FFFFFF0u) == 0x012FFF10u) {
        int rm = w & 0xFu;
        if (rm == 14) snprintf(buf, cap, "bx%s lr", cc);
        else snprintf(buf, cap, "bx%s r%d", cc, rm);
        return;
    }
    if ((w & 0x0FFF0000u) == 0x092D0000u) {
        snprintf(buf, cap, "push%s {0x%04X}", cc, (unsigned)(w & 0xFFFFu));
        return;
    }
    if ((w & 0x0FFF0000u) == 0x08BD0000u) {
        snprintf(buf, cap, "pop%s {0x%04X}", cc, (unsigned)(w & 0xFFFFu));
        return;
    }
    if (((w >> 26) & 3u) == 0u) {
        uint32_t op = (w >> 21) & 0xFu;
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        static const char *const ops[] = {
            "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
            "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"
        };
        const char *opname = ops[op];
        if (op == 13 || op == 15) {
            if ((w >> 25) & 1u)
                snprintf(buf, cap, "%s%s r%u, #%u", opname, cc, (unsigned)rd, (unsigned)(w & 0xFFu));
            else
                snprintf(buf, cap, "%s%s r%u, r%u", opname, cc, (unsigned)rd, (unsigned)(w & 0xFu));
            return;
        }
        if (op >= 8 && op <= 11) {
            if ((w >> 25) & 1u)
                snprintf(buf, cap, "%s%s r%u, #%u", opname, cc, (unsigned)rn, (unsigned)(w & 0xFFu));
            else
                snprintf(buf, cap, "%s%s r%u, r%u", opname, cc, (unsigned)rn, (unsigned)(w & 0xFu));
            return;
        }
        if ((w >> 25) & 1u)
            snprintf(buf, cap, "%s%s r%u, r%u, #%u", opname, cc, (unsigned)rd, (unsigned)rn, (unsigned)(w & 0xFFu));
        else
            snprintf(buf, cap, "%s%s r%u, r%u, r%u", opname, cc, (unsigned)rd, (unsigned)rn, (unsigned)(w & 0xFu));
        return;
    }
    if (((w >> 26) & 3u) == 1u) {
        int is_load = (w >> 20) & 1u;
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        uint32_t imm12 = w & 0xFFFu;
        int is_up = (w >> 23) & 1u;
        snprintf(buf, cap, "%s%s r%u, [r%u, #%s%u]", is_load ? "ldr" : "str", cc,
                 (unsigned)rd, (unsigned)rn, is_up ? "" : "-", (unsigned)imm12);
        return;
    }
    if (((w >> 24) & 0xFu) == 0xEu && ((w >> 8) & 0xFu) == 10u) {
        snprintf(buf, cap, "vfp (cp10) 0x%08X", (unsigned)w);
        return;
    }
    snprintf(buf, cap, ".word 0x%08X", (unsigned)w);
}

static void sc_disasm_viewer(sc_tab_t *t)
{
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    if (sc_compile_b(t, &blob, &blob_len) != 0)
        return;

#ifndef SHELL_CODE_HOST_TEST
    bmnative_mod *m = NULL;
    const char *why = "";
    if (bmnative_load(&m, blob, blob_len, &why) != BMN_OK) {
        free(blob);
        snprintf(t->status, sizeof(t->status), "Disasm: caricamento fallito: %s", why);
        return;
    }

    int top = 0;
    int page = g_sc.screen_rows - 2;
    if (page < 5) page = 5;

    for (;;) {
        console_clear();
        kprintf("\x1b[7m Disassembly: %s (%zu bytes, %lu words, %d fn) │ Up/Down, Esc/q:Esci \x1b[0m\r\n",
                t->name, blob_len, (unsigned long)m->ncode, m->nfn);

        for (int r = 0; r < page; r++) {
            uint32_t idx = (uint32_t)(top + r);
            if (idx >= m->ncode) break;

            for (int f = 0; f < m->nfn; f++) {
                if (m->fn[f].word_off == idx) {
                    kprintf("\x1b[1;93m; function %s(%d params) -> %d @ 0x%04X\x1b[0m\r\n",
                            m->fn[f].name, m->fn[f].nparams, m->fn[f].ret, (unsigned)(idx * 4));
                }
            }

            uint32_t w = m->code[idx];
            char dstr[64];
            sc_disasm_armv6_word(w, dstr, sizeof(dstr));
            kprintf("  \x1b[90m0x%04X:\x1b[0m \x1b[96m%08X\x1b[0m  %s\r\n", (unsigned)(idx * 4), (unsigned)w, dstr);
        }

        int c = (unsigned char)input_getc();
        if (c == 'q' || c == 'Q' || c == 0x1B) break;
        else if (c == HID_KEY_UP || c == 'k') { if (top > 0) top--; }
        else if (c == HID_KEY_DOWN || c == 'j') { if ((uint32_t)(top + 1) < m->ncode) top++; }
        else if (c == HID_KEY_PGUP) { top -= page; if (top < 0) top = 0; }
        else if (c == HID_KEY_PGDN) { top += page; if ((uint32_t)top >= m->ncode) top = m->ncode - 1; }
    }

    bmnative_free(m);
    free(blob);
    console_clear();
#else
    free(blob);
#endif
}

/* ---- Input Handling ---- */

static void sc_insert_text_char(sc_tab_t *t, char c)
{
    sc_line_insert_char(&t->lines[t->cy], t->cx, c);
    t->cx++;
    t->dirty = 1;
}

static void sc_insert_newline(sc_tab_t *t)
{
    sc_line_t *cur = &t->lines[t->cy];
    int indent = 0;
    while (indent < cur->len && cur->chars[indent] == ' ')
        indent++;

    const char *rest = cur->chars + t->cx;
    int rest_len = cur->len - t->cx;
    cur->len = t->cx;
    cur->chars[cur->len] = '\0';

    sc_tab_insert_line(t, t->cy + 1, NULL, 0);
    sc_line_t *next = &t->lines[t->cy + 1];
    for (int i = 0; i < indent; i++)
        sc_line_append(next, " ", 1);
    if (rest_len > 0)
        sc_line_append(next, rest, rest_len);

    t->cy++;
    t->cx = indent;
    t->dirty = 1;
}

static void sc_backspace(sc_tab_t *t)
{
    if (t->cx > 0) {
        sc_line_delete_char(&t->lines[t->cy], t->cx - 1);
        t->cx--;
        t->dirty = 1;
    } else if (t->cy > 0) {
        sc_line_t *prev = &t->lines[t->cy - 1];
        int prev_len = prev->len;
        sc_line_append(prev, t->lines[t->cy].chars, t->lines[t->cy].len);
        sc_tab_delete_line(t, t->cy);
        t->cy--;
        t->cx = prev_len;
        t->dirty = 1;
    }
}

static void sc_delete_under_cursor(sc_tab_t *t)
{
    sc_line_t *cur = &t->lines[t->cy];
    if (t->cx < cur->len) {
        sc_line_delete_char(cur, t->cx);
        t->dirty = 1;
    } else if (t->cy + 1 < t->nlines) {
        sc_line_append(cur, t->lines[t->cy + 1].chars, t->lines[t->cy + 1].len);
        sc_tab_delete_line(t, t->cy + 1);
        t->dirty = 1;
    }
}

static void sc_execute_menu_action(void)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];
    g_sc.show_menu = 0;

    switch (g_sc.menu_sel) {
    case MENU_RESUME:
        break;
    case MENU_SAVE:
        sc_tab_save(t, NULL);
        break;
    case MENU_SAVE_AS:
        /* Prompt or save */
        sc_tab_save(t, NULL);
        break;
    case MENU_RUN:
        sc_run_b(t, NULL);
        break;
    case MENU_DISASM:
        sc_disasm_viewer(t);
        break;
    case MENU_NEW:
        if (g_sc.ntabs < SC_MAX_TABS) {
            int new_idx = g_sc.ntabs++;
            char new_path[64];
            snprintf(new_path, sizeof(new_path), "/code-b/file%d.b", new_idx + 1);
            sc_tab_init_empty(&g_sc.tabs[new_idx], new_path);
            g_sc.cur_tab = new_idx;
        }
        break;
    case MENU_NEXT_TAB:
        g_sc.cur_tab = (g_sc.cur_tab + 1) % g_sc.ntabs;
        break;
    case MENU_CLOSE_TAB:
        if (g_sc.ntabs > 1) {
            sc_tab_free(&g_sc.tabs[g_sc.cur_tab]);
            for (int i = g_sc.cur_tab; i + 1 < g_sc.ntabs; i++)
                g_sc.tabs[i] = g_sc.tabs[i + 1];
            g_sc.ntabs--;
            if (g_sc.cur_tab >= g_sc.ntabs)
                g_sc.cur_tab = g_sc.ntabs - 1;
        } else {
            sc_tab_set_text(t, "", 0);
            t->dirty = 0;
        }
        break;
    case MENU_EXIT:
        g_sc.quit_requested = 1;
        break;
    default:
        break;
    }
}

static void sc_handle_key(int c)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];

    /* When popup menu is active */
    if (g_sc.show_menu) {
        if (c == 0x1B || c == 'q' || c == 'Q') {
            g_sc.show_menu = 0;
            return;
        }
        if (c == HID_KEY_UP || c == 'w' || c == 'k') {
            g_sc.menu_sel = (g_sc.menu_sel + MENU_COUNT - 1) % MENU_COUNT;
            return;
        }
        if (c == HID_KEY_DOWN || c == 's' || c == 'j') {
            g_sc.menu_sel = (g_sc.menu_sel + 1) % MENU_COUNT;
            return;
        }
        if (c == '\r' || c == '\n' || c == ' ') {
            sc_execute_menu_action();
            return;
        }
        return;
    }

    /* Global Esc: Open Popup Menu */
    if (c == 0x1B) {
        g_sc.show_menu = 1;
        g_sc.menu_sel = 0;
        return;
    }

    /* Global Hotkeys */
    if (c == 0x13) {                    /* Ctrl+S: Save */
        sc_tab_save(t, NULL);
        return;
    }
    if (c == 0x11) {                    /* Ctrl+Q: Quit */
        g_sc.quit_requested = 1;
        return;
    }
    if (c == 0x12 || c == SC_KEY_F5) {  /* Ctrl+R or F5: Run */
        sc_run_b(t, NULL);
        return;
    }
    if (c == 0x04 || c == SC_KEY_F7) {  /* Ctrl+D or F7: Disassembly */
        sc_disasm_viewer(t);
        return;
    }
    if (c == 0x14 || c == 0x17) {       /* Ctrl+T or Ctrl+W: Next tab */
        g_sc.cur_tab = (g_sc.cur_tab + 1) % g_sc.ntabs;
        return;
    }
    if (c == 0x0E) {                    /* Ctrl+N: New tab */
        if (g_sc.ntabs < SC_MAX_TABS) {
            int new_idx = g_sc.ntabs++;
            char new_path[64];
            snprintf(new_path, sizeof(new_path), "/code-b/file%d.b", new_idx + 1);
            sc_tab_init_empty(&g_sc.tabs[new_idx], new_path);
            g_sc.cur_tab = new_idx;
        }
        return;
    }

    /* Direct text editing */
    if (c == '\r' || c == '\n') {
        sc_insert_newline(t);
    } else if (c == 0x08 || c == 0x7F) {
        sc_backspace(t);
    } else if (c == HID_KEY_DEL) {
        sc_delete_under_cursor(t);
    } else if (c == '\t') {
        sc_insert_text_char(t, ' ');
        sc_insert_text_char(t, ' ');
    } else if (c == HID_KEY_LEFT) {
        if (t->cx > 0) t->cx--;
    } else if (c == HID_KEY_RIGHT) {
        if (t->cx < t->lines[t->cy].len) t->cx++;
    } else if (c == HID_KEY_UP) {
        if (t->cy > 0) {
            t->cy--;
            if (t->cx > t->lines[t->cy].len) t->cx = t->lines[t->cy].len;
        }
    } else if (c == HID_KEY_DOWN) {
        if (t->cy + 1 < t->nlines) {
            t->cy++;
            if (t->cx > t->lines[t->cy].len) t->cx = t->lines[t->cy].len;
        }
    } else if (c == HID_KEY_HOME) {
        t->cx = 0;
    } else if (c == HID_KEY_END) {
        t->cx = t->lines[t->cy].len;
    } else if (c == HID_KEY_PGUP) {
        t->cy -= (g_sc.screen_rows - 2);
        if (t->cy < 0) t->cy = 0;
    } else if (c == HID_KEY_PGDN) {
        t->cy += (g_sc.screen_rows - 2);
        if (t->cy >= t->nlines) t->cy = t->nlines - 1;
    } else if (c >= 32 && c < 127) {
        sc_insert_text_char(t, (char)c);
    }
    sc_scroll_to_cursor(t);
}

/* ---- Main Entry Point ---- */

int shell_code_open(const char *path)
{
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.screen_cols = 80;
    g_sc.screen_rows = 25;

#ifndef SHELL_CODE_HOST_TEST
    uint32_t cols = 80, rows = 25;
    console_size(&cols, &rows);
    if (cols > 20) g_sc.screen_cols = (int)cols;
    if (rows > 5) g_sc.screen_rows = (int)rows;
#endif

    g_sc.ntabs = 1;
    g_sc.cur_tab = 0;
    g_sc.show_menu = 0;
    g_sc.menu_sel = 0;

    char norm[SC_PATH_MAX];
    sc_normalize_path(norm, sizeof(norm), path);
    sc_tab_load(&g_sc.tabs[0], norm);

#ifndef SHELL_CODE_HOST_TEST
    hid_text_mode(1);
    console_clear();

    while (!g_sc.quit_requested) {
        sc_draw_tabs_status();
        sc_draw_editor();
        if (g_sc.show_menu)
            sc_draw_popup_menu();

        int c = (unsigned char)input_getc();
        sc_handle_key(c);
    }

    hid_text_mode(0);
    console_clear();
#endif

    for (int i = 0; i < g_sc.ntabs; i++) {
        sc_tab_free(&g_sc.tabs[i]);
    }
    return 0;
}

/* ---- Host Unit Test Suite ---- */

int shell_code_test_run(void)
{
    printf("running shell_code tests...\n");

    /* Test 1: Path normalization into /code-b/ */
    char norm[SC_PATH_MAX];
    sc_normalize_path(norm, sizeof(norm), "test.b");
    if (strcmp(norm, "/code-b/test.b") != 0) {
        printf("FAIL: path norm (%s != /code-b/test.b)\n", norm);
        return 1;
    }
    sc_normalize_path(norm, sizeof(norm), NULL);
    if (strcmp(norm, "/code-b/scratch.b") != 0) {
        printf("FAIL: null path norm (%s != /code-b/scratch.b)\n", norm);
        return 2;
    }

    /* Test 2: Init tab and text operations */
    sc_tab_t tab;
    sc_tab_init_empty(&tab, "test.b");
    if (tab.nlines != 1 || tab.syntax != SC_SYN_B) {
        printf("FAIL: tab init syntax or nlines\n");
        return 3;
    }

    /* Test 3: Insert text and verify lines */
    const char *test_src =
        "function sum(n: i32): i32\n"
        "  local s = 0\n"
        "  for i = 1, n do s = s + i end\n"
        "  return s\n"
        "end\n";
    sc_tab_set_text(&tab, test_src, strlen(test_src));
    if (tab.nlines != 5) {
        printf("FAIL: set_text lines count (%d != 5)\n", tab.nlines);
        return 4;
    }

    /* Test 4: Reconstruct text */
    size_t out_len = 0;
    char *text = sc_tab_get_text(&tab, &out_len);
    if (!text || strcmp(text, test_src) != 0) {
        printf("FAIL: get_text mismatch\n");
        free(text);
        return 5;
    }
    free(text);

    /* Test 5: Compile .b natively */
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    int rc = sc_compile_b(&tab, &blob, &blob_len);
    if (rc != 0 || !blob || blob_len < 12) {
        printf("FAIL: sc_compile_b failed (%d)\n", rc);
        free(blob);
        return 6;
    }

    /* Test 6: Verify BMN1 container magic and header */
    uint32_t magic = (uint32_t)blob[0] | ((uint32_t)blob[1] << 8) | ((uint32_t)blob[2] << 16) | ((uint32_t)blob[3] << 24);
    if (magic != BMN_MAGIC) {
        printf("FAIL: BMN1 magic mismatch (0x%08X)\n", (unsigned)magic);
        free(blob);
        return 7;
    }

    /* Test 7: Verify BMN1 execution on host via bmnative_load + call */
    bmnative_mod *m = NULL;
    const char *why = "";
    int lrc = bmnative_load(&m, blob, blob_len, &why);
    if (lrc != BMN_OK) {
        printf("FAIL: bmnative_load: %s\n", why);
        free(blob);
        return 8;
    }
    int fn_idx = bmnative_find(m, "sum");
    if (fn_idx < 0) {
        printf("FAIL: bmnative_find 'sum'\n");
        bmnative_free(m);
        free(blob);
        return 9;
    }
    int32_t args[1] = { 10 };
    int32_t ret = 0;
    int crc = bmnative_call(m, fn_idx, args, 1, &ret);
    if (crc != BMN_OK || ret != 55) {
        printf("FAIL: sum(10) returned %ld (expected 55)\n", (long)ret);
        bmnative_free(m);
        free(blob);
        return 10;
    }
    bmnative_free(m);
    free(blob);

    /* Test 8: Compiler error feedback and line location */
    const char *bad_src =
        "function broken(): i32\n"
        "  local x = undefined_var + 1\n"
        "  return x\n"
        "end\n";
    sc_tab_set_text(&tab, bad_src, strlen(bad_src));
    rc = sc_compile_b(&tab, &blob, &blob_len);
    if (rc == 0 || tab.err_line != 2) {
        printf("FAIL: compiler error line mismatch (%d != 2)\n", tab.err_line);
        return 11;
    }

    /* Test 9: Disassembly formatting */
    char dis_buf[64];
    sc_disasm_armv6_word(0xE2810002u, dis_buf, sizeof(dis_buf)); /* add r0, r1, #2 */
    if (strstr(dis_buf, "add") == NULL) {
        printf("FAIL: disasm opcode mismatch (%s)\n", dis_buf);
        return 12;
    }

    /* Test 10: Direct typing, Esc Popup Menu navigation and Exit */
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.screen_cols = 80;
    g_sc.screen_rows = 25;
    g_sc.ntabs = 1;
    g_sc.cur_tab = 0;
    sc_tab_init_empty(&g_sc.tabs[0], "/code-b/scratch.b");

    /* Type "local v = 42" */
    const char *typed = "local v = 42";
    for (size_t k = 0; k < strlen(typed); k++)
        sc_handle_key(typed[k]);
    if (g_sc.tabs[0].lines[0].len != (int)strlen(typed)) {
        printf("FAIL: direct typing length mismatch\n");
        return 13;
    }

    /* Press Esc: opens popup menu */
    sc_handle_key(0x1B);
    if (!g_sc.show_menu) {
        printf("FAIL: Esc did not open popup menu\n");
        return 14;
    }

    /* Move down in menu and select Salva */
    sc_handle_key(HID_KEY_DOWN);
    if (g_sc.menu_sel != 1) {
        printf("FAIL: menu navigation down mismatch (%d != 1)\n", g_sc.menu_sel);
        return 15;
    }
    sc_handle_key('\n'); /* Execute Salva */
    if (g_sc.show_menu) {
        printf("FAIL: Salva did not close menu\n");
        return 16;
    }

    /* Draw test */
    sc_draw_tabs_status();
    sc_draw_editor();
    sc_draw_popup_menu();

    sc_tab_free(&g_sc.tabs[0]);
    sc_tab_free(&tab);
    printf("shell_code tests passed: 10/10 checks ok\n");
    return 0;
}
