/*
 * Shell Code: Hybrid Vim / bm Code editor, compiler, and runner for bm shell.
 *
 * Implements:
 * - Multi-tab text buffer with dynamic lines
 * - Line numbering gutter with error markers
 * - Syntax highlighting for .b, .c, .lua, .txt
 * - Modal Vim navigation (Normal, Insert, Command, Search) and bm Code shortcuts
 * - Compiler integration with bml_compile_native() and exact error location
 * - Native ARMv6 runner with microsecond profiling and framebuffer graphics
 * - ARMv6 A32 / BMN1 interactive disassembly viewer
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
#define HID_KEY_F5      0xFD
#define HID_KEY_F7      0xE7
#endif
#endif

#define SC_MAX_TABS    8
#define SC_PATH_MAX    128
#define SC_LINE_INIT   64

#ifndef HID_KEY_F1
#define HID_KEY_F1     0xF9
#endif
#ifndef HID_KEY_F6
#define HID_KEY_F6     0xE6
#endif
#define SC_KEY_F5      (HID_KEY_F1 + 4)
#define SC_KEY_F7      (HID_KEY_F6 + 1)

enum {
    SC_SYN_PLAIN = 0,
    SC_SYN_B     = 1,
    SC_SYN_LUA   = 2,
    SC_SYN_C     = 3
};

enum {
    SC_MODE_NORMAL  = 0,
    SC_MODE_INSERT  = 1,
    SC_MODE_COMMAND = 2,
    SC_MODE_SEARCH  = 3
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

typedef struct {
    sc_tab_t tabs[SC_MAX_TABS];
    int      ntabs;
    int      cur_tab;
    int      mode;
    char     cmd_buf[128];
    int      cmd_len;
    char     search_buf[64];
    int      search_len;
    char    *yank_buf;
    int      yank_is_line;
    int      quit_requested;
    int      screen_cols;
    int      screen_rows;
} sc_state_t;

static sc_state_t g_sc;

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
    if (path && *path)
        strncpy(t->path, path, sizeof(t->path) - 1);
    else
        strcpy(t->path, "scratch.b");
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
    char resolved[128];
    if (mfs_path(path, resolved, sizeof(resolved)) != 0) {
        strncpy(resolved, path, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }
    fat_entry_t e;
    if (fat_find(resolved, &e) == 0 && !e.is_dir) {
        uint8_t *data = NULL;
        size_t len = 0;
        if (fat_load(&e, &data, &len) == 0) {
            sc_tab_set_text(t, (const char *)data, len);
            free(data);
            t->dirty = 0;
            snprintf(t->status, sizeof(t->status), "Loaded %s (%zu bytes)", resolved, len);
            return 0;
        }
    }
#endif
    snprintf(t->status, sizeof(t->status), "New file: %s", path);
    return 0;
}

static int sc_tab_save(sc_tab_t *t, const char *override_path)
{
    const char *target = (override_path && *override_path) ? override_path : t->path;
    if (!target || !*target)
        target = "scratch.b";
    snprintf(t->path, sizeof(t->path), "%s", target);
    sc_tab_update_name(t);
    sc_tab_detect_syntax(t);

    size_t len = 0;
    char *text = sc_tab_get_text(t, &len);
    if (!text) {
        snprintf(t->status, sizeof(t->status), "Error: out of memory");
        return -1;
    }

#ifndef SHELL_CODE_HOST_TEST
    char resolved[128];
    if (mfs_path(target, resolved, sizeof(resolved)) != 0) {
        strncpy(resolved, target, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }
    fat_entry_t e;
    int ok = 0;
    if (fat_find(resolved, &e) == 0 && !e.is_dir) {
        ok = (fat_replace(resolved, text, len) == 0);
    } else {
        char dir[128];
        snprintf(dir, sizeof(dir), "%s", resolved);
        char *slash = strrchr(dir, '/');
        const char *name = resolved;
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
        snprintf(t->status, sizeof(t->status), "Saved %zu bytes to %s", len, resolved);
        return 0;
    } else {
        snprintf(t->status, sizeof(t->status), "Save failed: %s", fat_error());
        return -1;
    }
#else
    free(text);
    t->dirty = 0;
    snprintf(t->status, sizeof(t->status), "Saved %zu bytes to %s", len, target);
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

static void sc_format_line_syntax(const sc_tab_t *t, int line_idx, int col_off, int max_cols, char *out, size_t max_len)
{
    if (line_idx < 0 || line_idx >= t->nlines) {
        out[0] = '\0';
        return;
    }
    const sc_line_t *l = &t->lines[line_idx];
    const char *s = l->chars;
    int len = l->len;
    size_t pos = 0;

    int i = 0;
    while (i < len) {
        int visible = (i >= col_off && i < col_off + max_cols);

        /* Comments */
        if ((t->syntax == SC_SYN_B || t->syntax == SC_SYN_LUA) && i + 1 < len && s[i] == '-' && s[i + 1] == '-') {
            if (visible && pos + 5 < max_len) {
                memcpy(out + pos, "\x1b[90m", 5);
                pos += 5;
            }
            while (i < len) {
                if (i >= col_off && i < col_off + max_cols && pos + 1 < max_len)
                    out[pos++] = s[i];
                i++;
            }
            if (pos + 4 < max_len) {
                memcpy(out + pos, "\x1b[0m", 4);
                pos += 4;
            }
            break;
        }
        if (t->syntax == SC_SYN_C && i + 1 < len && s[i] == '/' && s[i + 1] == '/') {
            if (visible && pos + 5 < max_len) {
                memcpy(out + pos, "\x1b[90m", 5);
                pos += 5;
            }
            while (i < len) {
                if (i >= col_off && i < col_off + max_cols && pos + 1 < max_len)
                    out[pos++] = s[i];
                i++;
            }
            if (pos + 4 < max_len) {
                memcpy(out + pos, "\x1b[0m", 4);
                pos += 4;
            }
            break;
        }

        /* Strings */
        if (s[i] == '"' || (t->syntax == SC_SYN_LUA && s[i] == '\'')) {
            char quote = s[i];
            if (visible && pos + 5 < max_len) {
                memcpy(out + pos, "\x1b[33m", 5);
                pos += 5;
            }
            if (visible && pos + 1 < max_len)
                out[pos++] = s[i];
            i++;
            while (i < len && s[i] != quote) {
                if (s[i] == '\\' && i + 1 < len) {
                    if (i >= col_off && i < col_off + max_cols && pos + 1 < max_len)
                        out[pos++] = s[i];
                    i++;
                }
                if (i >= col_off && i < col_off + max_cols && pos + 1 < max_len)
                    out[pos++] = s[i];
                i++;
            }
            if (i < len) {
                if (i >= col_off && i < col_off + max_cols && pos + 1 < max_len)
                    out[pos++] = s[i];
                i++;
            }
            if (pos + 4 < max_len) {
                memcpy(out + pos, "\x1b[0m", 4);
                pos += 4;
            }
            continue;
        }

        /* Identifiers and Keywords */
        if (is_ident_start(s[i])) {
            int start = i;
            while (i < len && is_ident_part(s[i]))
                i++;
            int tok_len = i - start;
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
                else if ((tok_len == 4 && strncmp(s + start, "true", 4) == 0) ||
                         (tok_len == 5 && strncmp(s + start, "false", 5) == 0))
                    color = "\x1b[35m";
            } else if (t->syntax == SC_SYN_C) {
                if (sc_match_kw(s + start, tok_len, c_kw, sizeof(c_kw)/sizeof(c_kw[0])))
                    color = "\x1b[1;96m";
            }

            for (int k = start; k < i; k++) {
                if (k >= col_off && k < col_off + max_cols) {
                    if (color && k == start && pos + strlen(color) < max_len) {
                        memcpy(out + pos, color, strlen(color));
                        pos += strlen(color);
                    }
                    if (pos + 1 < max_len)
                        out[pos++] = s[k];
                }
            }
            if (color && pos + 4 < max_len) {
                memcpy(out + pos, "\x1b[0m", 4);
                pos += 4;
            }
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)s[i])) {
            int start = i;
            while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '.'))
                i++;
            for (int k = start; k < i; k++) {
                if (k >= col_off && k < col_off + max_cols) {
                    if (k == start && pos + 5 < max_len) {
                        memcpy(out + pos, "\x1b[35m", 5);
                        pos += 5;
                    }
                    if (pos + 1 < max_len)
                        out[pos++] = s[k];
                }
            }
            if (pos + 4 < max_len) {
                memcpy(out + pos, "\x1b[0m", 4);
                pos += 4;
            }
            continue;
        }

        /* Plain char */
        if (visible && pos + 1 < max_len)
            out[pos++] = s[i];
        i++;
    }
    out[pos] = '\0';
}

/* ---- Screen Drawing ---- */

static void sc_draw_tabs(void)
{
    char buf[256];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\x1b[H\x1b[7m");
    for (int i = 0; i < g_sc.ntabs; i++) {
        sc_tab_t *t = &g_sc.tabs[i];
        if (i == g_sc.cur_tab) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\x1b[1;30;46m [%d: %s%s] \x1b[7m",
                            i + 1, t->name, t->dirty ? " *" : "");
        } else {
            pos += snprintf(buf + pos, sizeof(buf) - pos, " %d: %s%s ",
                            i + 1, t->name, t->dirty ? " *" : "");
        }
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\x1b[K\x1b[0m\r\n");
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif
}

static void sc_draw_editor(void)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];
    int rows = g_sc.screen_rows - 3;
    if (rows < 1) rows = 1;
    int cols = g_sc.screen_cols;

    /* Gutter width: 4 or 5 chars */
    int gw = (t->nlines >= 1000) ? 6 : 5;
    int text_cols = cols - gw;
    if (text_cols < 10) text_cols = 10;

    char line_buf[512];
    char text_buf[512];

    for (int r = 0; r < rows; r++) {
        int file_line = t->row_off + r;
        if (file_line < t->nlines) {
            int is_err = (file_line + 1 == t->err_line);
            int written = 0;
            if (is_err) {
                written = snprintf(line_buf, sizeof(line_buf), "\x1b[1;91m!%*d\x1b[0m\x1b[90m\xb3\x1b[0m", gw - 1, file_line + 1);
            } else {
                written = snprintf(line_buf, sizeof(line_buf), "\x1b[90m%*d\xb3\x1b[0m", gw - 1, file_line + 1);
            }
            if (written < 0 || written >= (int)sizeof(line_buf)) written = 0;
            sc_format_line_syntax(t, file_line, t->col_off, text_cols, text_buf, sizeof(text_buf));
            snprintf(line_buf + written, sizeof(line_buf) - (size_t)written, "%s\x1b[K\r\n", text_buf);
        } else {
            snprintf(line_buf, sizeof(line_buf), "\x1b[94m~\x1b[0m\x1b[K\r\n");
        }
#ifndef SHELL_CODE_HOST_TEST
        console_write(line_buf);
#endif
    }

    /* Status Bar */
    const char *modestr = "-- NORMAL --";
    if (g_sc.mode == SC_MODE_INSERT) modestr = "-- INSERT --";
    else if (g_sc.mode == SC_MODE_COMMAND) modestr = "-- COMMAND --";
    else if (g_sc.mode == SC_MODE_SEARCH) modestr = "-- SEARCH --";

    const char *synstr = ".b";
    if (t->syntax == SC_SYN_LUA) synstr = ".lua";
    else if (t->syntax == SC_SYN_C) synstr = ".c";
    else if (t->syntax == SC_SYN_PLAIN) synstr = ".txt";

    char status[256];
    snprintf(status, sizeof(status), "\x1b[7m %-12s %s%s  [%s]  Ln %d/%d, Col %d  │ ^S:Save ^R:Run ^D:Asm ^Q:Quit\x1b[K\x1b[0m\r\n",
             modestr, t->name, t->dirty ? " *" : "", synstr, t->cy + 1, t->nlines, t->cx + 1);
#ifndef SHELL_CODE_HOST_TEST
    console_write(status);
#endif

    /* Command / Message Row */
    char bottom[256];
    if (g_sc.mode == SC_MODE_COMMAND) {
        snprintf(bottom, sizeof(bottom), ":%s\x1b[K", g_sc.cmd_buf);
    } else if (g_sc.mode == SC_MODE_SEARCH) {
        snprintf(bottom, sizeof(bottom), "/%s\x1b[K", g_sc.search_buf);
    } else if (t->err_line > 0) {
        snprintf(bottom, sizeof(bottom), "\x1b[1;91m%s\x1b[0m\x1b[K", t->status);
    } else {
        snprintf(bottom, sizeof(bottom), "\x1b[92m%s\x1b[0m\x1b[K", t->status);
    }
#ifndef SHELL_CODE_HOST_TEST
    console_write(bottom);
#endif

    /* Position hardware / ANSI cursor */
    int cursor_r = 2 + (t->cy - t->row_off);
    int cursor_c = 1 + gw + (t->cx - t->col_off);
    if (g_sc.mode == SC_MODE_COMMAND) {
        cursor_r = g_sc.screen_rows;
        cursor_c = 2 + g_sc.cmd_len;
    } else if (g_sc.mode == SC_MODE_SEARCH) {
        cursor_r = g_sc.screen_rows;
        cursor_c = 2 + g_sc.search_len;
    }
    char curpos[32];
    snprintf(curpos, sizeof(curpos), "\x1b[%d;%dH", cursor_r, cursor_c);
#ifndef SHELL_CODE_HOST_TEST
    console_write(curpos);
#endif
}

static void sc_scroll_to_cursor(sc_tab_t *t)
{
    int rows = g_sc.screen_rows - 3;
    if (rows < 1) rows = 1;
    int gw = (t->nlines >= 1000) ? 6 : 5;
    int text_cols = g_sc.screen_cols - gw;
    if (text_cols < 10) text_cols = 10;

    if (t->cy < t->row_off)
        t->row_off = t->cy;
    if (t->cy >= t->row_off + rows)
        t->row_off = t->cy - rows + 1;

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
        snprintf(t->status, sizeof(t->status), "Out of memory");
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
        snprintf(t->status, sizeof(t->status), "Compile Error: %s", err);
        return -1;
    }

    t->err_line = 0;
    snprintf(t->status, sizeof(t->status), "Compiled OK: %zu bytes BMN1 ARMv6", *out_len);
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
            /* Draw 8x8 sprite placeholder */
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
        snprintf(t->status, sizeof(t->status), "Load failed: %s", why);
        return -1;
    }

    /* Choose function: first search function under cursor, else main, else fn[0] */
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
        args[0] = 10;   /* sensible default, e.g. for sum(10) */
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
        /* Graphical output: wait for key press to return to editor */
        kprintf("\n\x1b[1;92m-- Graphics drawn. %s() returned %d (%u us). Press any key to return --\x1b[0m\n",
                f->name, ret, us);
        input_getc();
        console_clear();
    }

    if (err == BMN_OK) {
        snprintf(t->status, sizeof(t->status), "%s() = %d (0x%X) [%u us]",
                 f->name, ret, (unsigned)ret, us);
    } else {
        snprintf(t->status, sizeof(t->status), "Execution trap (%d) in %s", err, f->name);
    }

    bmnative_free(m);
    free(blob);
    return (err == BMN_OK) ? 0 : -1;
#else
    (void)arg_str;
    free(blob);
    snprintf(t->status, sizeof(t->status), "Compiled OK (runner simulated)");
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

    /* Branch B / BL */
    if (((w >> 25) & 7u) == 5u) {
        int is_bl = (w >> 24) & 1u;
        int32_t imm24 = (int32_t)(w & 0xFFFFFFu);
        if (imm24 & 0x800000) imm24 -= 0x1000000;
        int32_t byte_off = (imm24 + 2) * 4;
        snprintf(buf, cap, "%s%s %+d (target pc%+d)", is_bl ? "bl" : "b", cc, byte_off, byte_off);
        return;
    }
    /* BX LR */
    if ((w & 0x0FFFFFF0u) == 0x012FFF10u) {
        int rm = w & 0xFu;
        if (rm == 14) snprintf(buf, cap, "bx%s lr", cc);
        else snprintf(buf, cap, "bx%s r%d", cc, rm);
        return;
    }
    /* PUSH / POP */
    if ((w & 0x0FFF0000u) == 0x092D0000u) {
        snprintf(buf, cap, "push%s {regs: 0x%04X}", cc, w & 0xFFFFu);
        return;
    }
    if ((w & 0x0FFF0000u) == 0x08BD0000u) {
        snprintf(buf, cap, "pop%s {regs: 0x%04X}", cc, w & 0xFFFFu);
        return;
    }
    /* Data processing: MOV, ADD, SUB, RSB, CMP, etc. */
    if (((w >> 26) & 3u) == 0u) {
        uint32_t op = (w >> 21) & 0xFu;
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        static const char *const ops[] = {
            "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
            "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"
        };
        const char *opname = ops[op];
        if (op == 13 || op == 15) {     /* MOV / MVN */
            if ((w >> 25) & 1u)
                snprintf(buf, cap, "%s%s r%u, #%u", opname, cc, rd, w & 0xFFu);
            else
                snprintf(buf, cap, "%s%s r%u, r%u", opname, cc, rd, w & 0xFu);
            return;
        }
        if (op == 8 || op == 9 || op == 10 || op == 11) { /* CMP / TST */
            if ((w >> 25) & 1u)
                snprintf(buf, cap, "%s%s r%u, #%u", opname, cc, rn, w & 0xFFu);
            else
                snprintf(buf, cap, "%s%s r%u, r%u", opname, cc, rn, w & 0xFu);
            return;
        }
        /* ADD, SUB, etc. */
        if ((w >> 25) & 1u)
            snprintf(buf, cap, "%s%s r%u, r%u, #%u", opname, cc, rd, rn, w & 0xFFu);
        else
            snprintf(buf, cap, "%s%s r%u, r%u, r%u", opname, cc, rd, rn, w & 0xFu);
        return;
    }
    /* Memory LDR / STR */
    if (((w >> 26) & 3u) == 1u) {
        int is_load = (w >> 20) & 1u;
        uint32_t rn = (w >> 16) & 0xFu;
        uint32_t rd = (w >> 12) & 0xFu;
        uint32_t imm12 = w & 0xFFFu;
        int is_up = (w >> 23) & 1u;
        snprintf(buf, cap, "%s%s r%u, [r%u, #%s%u]", is_load ? "ldr" : "str", cc, rd, rn, is_up ? "" : "-", imm12);
        return;
    }
    /* VFP instructions (coprocessor 10) */
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
        snprintf(t->status, sizeof(t->status), "Disasm: load failed: %s", why);
        return;
    }

    int top = 0;
    int page = g_sc.screen_rows - 2;
    if (page < 5) page = 5;

    for (;;) {
        console_clear();
        kprintf("\x1b[7m BMN1 Disassembly: %s (%zu bytes, %u words, %d fn) │ Up/Down, q:Return \x1b[0m\r\n",
                t->name, blob_len, (unsigned)m->ncode, m->nfn);

        for (int r = 0; r < page; r++) {
            uint32_t idx = (uint32_t)(top + r);
            if (idx >= m->ncode) break;

            /* Check if index is function entry */
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

static void sc_execute_command(void)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];
    char *cmd = g_sc.cmd_buf;
    while (*cmd == ' ') cmd++;

    if (strcmp(cmd, "q") == 0) {
        if (t->dirty) {
            snprintf(t->status, sizeof(t->status), "Unsaved changes! Use :q! or :wq");
        } else {
            g_sc.quit_requested = 1;
        }
    } else if (strcmp(cmd, "q!") == 0) {
        g_sc.quit_requested = 1;
    } else if (strncmp(cmd, "w", 1) == 0 && (cmd[1] == '\0' || cmd[1] == ' ')) {
        const char *arg = cmd + 1;
        while (*arg == ' ') arg++;
        sc_tab_save(t, arg);
    } else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        if (sc_tab_save(t, NULL) == 0)
            g_sc.quit_requested = 1;
    } else if (strncmp(cmd, "e ", 2) == 0) {
        const char *file = cmd + 2;
        while (*file == ' ') file++;
        if (g_sc.ntabs < SC_MAX_TABS) {
            int new_idx = g_sc.ntabs++;
            sc_tab_load(&g_sc.tabs[new_idx], file);
            g_sc.cur_tab = new_idx;
        } else {
            sc_tab_load(t, file);
        }
    } else if (strncmp(cmd, "r", 1) == 0 || strncmp(cmd, "run", 3) == 0) {
        const char *arg = strchr(cmd, ' ');
        if (arg) while (*arg == ' ') arg++;
        sc_run_b(t, arg);
    } else if (strcmp(cmd, "dis") == 0 || strcmp(cmd, "asm") == 0) {
        sc_disasm_viewer(t);
    } else {
        snprintf(t->status, sizeof(t->status), "Unknown command: :%s", cmd);
    }
}

static void sc_handle_key(int c)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];

    /* Global shortcuts */
    if (c == 0x13) {                    /* Ctrl+S: Save */
        sc_tab_save(t, NULL);
        return;
    }
    if (c == 0x11) {                    /* Ctrl+Q: Quit */
        if (t->dirty) {
            snprintf(t->status, sizeof(t->status), "Unsaved changes! Use :q! or :wq");
        } else {
            g_sc.quit_requested = 1;
        }
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

    if (g_sc.mode == SC_MODE_INSERT) {
        if (c == 0x1B) {                /* Esc: Return to NORMAL */
            g_sc.mode = SC_MODE_NORMAL;
            if (t->cx > 0) t->cx--;
            return;
        }
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
        } else if (c >= 32 && c < 127) {
            sc_insert_text_char(t, (char)c);
        }
        sc_scroll_to_cursor(t);
        return;
    }

    if (g_sc.mode == SC_MODE_COMMAND) {
        if (c == 0x1B) {                /* Esc: Cancel */
            g_sc.mode = SC_MODE_NORMAL;
            g_sc.cmd_len = 0;
            g_sc.cmd_buf[0] = '\0';
            return;
        }
        if (c == '\r' || c == '\n') {
            g_sc.mode = SC_MODE_NORMAL;
            sc_execute_command();
            g_sc.cmd_len = 0;
            g_sc.cmd_buf[0] = '\0';
            return;
        }
        if (c == 0x08 || c == 0x7F) {
            if (g_sc.cmd_len > 0) {
                g_sc.cmd_len--;
                g_sc.cmd_buf[g_sc.cmd_len] = '\0';
            } else {
                g_sc.mode = SC_MODE_NORMAL;
            }
            return;
        }
        if (c >= 32 && c < 127 && g_sc.cmd_len + 1 < (int)sizeof(g_sc.cmd_buf)) {
            g_sc.cmd_buf[g_sc.cmd_len++] = (char)c;
            g_sc.cmd_buf[g_sc.cmd_len] = '\0';
        }
        return;
    }

    /* MODE_NORMAL */
    if (c == 'i') {
        g_sc.mode = SC_MODE_INSERT;
        return;
    }
    if (c == 'a') {
        g_sc.mode = SC_MODE_INSERT;
        if (t->cx < t->lines[t->cy].len) t->cx++;
        return;
    }
    if (c == 'I') {
        g_sc.mode = SC_MODE_INSERT;
        t->cx = 0;
        while (t->cx < t->lines[t->cy].len && t->lines[t->cy].chars[t->cx] == ' ')
            t->cx++;
        return;
    }
    if (c == 'A') {
        g_sc.mode = SC_MODE_INSERT;
        t->cx = t->lines[t->cy].len;
        return;
    }
    if (c == 'o') {
        g_sc.mode = SC_MODE_INSERT;
        t->cx = t->lines[t->cy].len;
        sc_insert_newline(t);
        return;
    }
    if (c == 'O') {
        g_sc.mode = SC_MODE_INSERT;
        sc_tab_insert_line(t, t->cy, "", 0);
        t->cx = 0;
        return;
    }
    if (c == ':') {
        g_sc.mode = SC_MODE_COMMAND;
        g_sc.cmd_len = 0;
        g_sc.cmd_buf[0] = '\0';
        return;
    }
    if (c == 'h' || c == HID_KEY_LEFT) {
        if (t->cx > 0) t->cx--;
    } else if (c == 'l' || c == HID_KEY_RIGHT) {
        if (t->cx + 1 < t->lines[t->cy].len) t->cx++;
    } else if (c == 'k' || c == HID_KEY_UP) {
        if (t->cy > 0) {
            t->cy--;
            if (t->cx >= t->lines[t->cy].len)
                t->cx = t->lines[t->cy].len ? t->lines[t->cy].len - 1 : 0;
        }
    } else if (c == 'j' || c == HID_KEY_DOWN) {
        if (t->cy + 1 < t->nlines) {
            t->cy++;
            if (t->cx >= t->lines[t->cy].len)
                t->cx = t->lines[t->cy].len ? t->lines[t->cy].len - 1 : 0;
        }
    } else if (c == '0' || c == HID_KEY_HOME) {
        t->cx = 0;
    } else if (c == '$' || c == HID_KEY_END) {
        t->cx = t->lines[t->cy].len ? t->lines[t->cy].len - 1 : 0;
    } else if (c == 'x') {
        sc_delete_under_cursor(t);
    } else if (c == 'd') {
        /* dd deletes line */
        sc_tab_delete_line(t, t->cy);
        if (t->cy >= t->nlines) t->cy = t->nlines - 1;
        t->cx = 0;
    } else if (c == 'G') {
        t->cy = t->nlines - 1;
        t->cx = 0;
    } else if (c == 'g') {
        t->cy = 0;
        t->cx = 0;
    } else if (c == HID_KEY_PGUP) {
        t->cy -= (g_sc.screen_rows - 3);
        if (t->cy < 0) t->cy = 0;
    } else if (c == HID_KEY_PGDN) {
        t->cy += (g_sc.screen_rows - 3);
        if (t->cy >= t->nlines) t->cy = t->nlines - 1;
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
    sc_tab_load(&g_sc.tabs[0], path);

#ifndef SHELL_CODE_HOST_TEST
    hid_text_mode(1);
    console_clear();

    while (!g_sc.quit_requested) {
        sc_draw_tabs();
        sc_draw_editor();

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

    /* Test 1: Init tab and text operations */
    sc_tab_t tab;
    sc_tab_init_empty(&tab, "test.b");
    if (tab.nlines != 1 || tab.syntax != SC_SYN_B) {
        printf("FAIL: tab init syntax or nlines\n");
        return 1;
    }

    /* Test 2: Insert text and verify lines */
    const char *test_src =
        "function sum(n: i32): i32\n"
        "  local s = 0\n"
        "  for i = 1, n do s = s + i end\n"
        "  return s\n"
        "end\n";
    sc_tab_set_text(&tab, test_src, strlen(test_src));
    if (tab.nlines != 5) {
        printf("FAIL: set_text lines count (%d != 5)\n", tab.nlines);
        return 2;
    }

    /* Test 3: Reconstruct text */
    size_t out_len = 0;
    char *text = sc_tab_get_text(&tab, &out_len);
    if (!text || strcmp(text, test_src) != 0) {
        printf("FAIL: get_text mismatch\n");
        free(text);
        return 3;
    }
    free(text);

    /* Test 4: Compile .b natively */
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    int rc = sc_compile_b(&tab, &blob, &blob_len);
    if (rc != 0 || !blob || blob_len < 12) {
        printf("FAIL: sc_compile_b failed (%d)\n", rc);
        free(blob);
        return 4;
    }

    /* Test 5: Verify BMN1 container magic and header */
    uint32_t magic = (uint32_t)blob[0] | ((uint32_t)blob[1] << 8) | ((uint32_t)blob[2] << 16) | ((uint32_t)blob[3] << 24);
    if (magic != BMN_MAGIC) {
        printf("FAIL: BMN1 magic mismatch (0x%08X)\n", (unsigned)magic);
        free(blob);
        return 5;
    }

    /* Test 6: Verify BMN1 execution on host via bmnative_load + call */
    bmnative_mod *m = NULL;
    const char *why = "";
    int lrc = bmnative_load(&m, blob, blob_len, &why);
    if (lrc != BMN_OK) {
        printf("FAIL: bmnative_load: %s\n", why);
        free(blob);
        return 6;
    }
    int fn_idx = bmnative_find(m, "sum");
    if (fn_idx < 0) {
        printf("FAIL: bmnative_find 'sum'\n");
        bmnative_free(m);
        free(blob);
        return 7;
    }
    int32_t args[1] = { 10 };
    int32_t ret = 0;
    int crc = bmnative_call(m, fn_idx, args, 1, &ret);
    if (crc != BMN_OK || ret != 55) {
        printf("FAIL: sum(10) returned %ld (expected 55)\n", (long)ret);
        bmnative_free(m);
        free(blob);
        return 8;
    }
    bmnative_free(m);
    free(blob);

    /* Test 7: Compiler error feedback and line location */
    const char *bad_src =
        "function broken(): i32\n"
        "  local x = undefined_var + 1\n"
        "  return x\n"
        "end\n";
    sc_tab_set_text(&tab, bad_src, strlen(bad_src));
    rc = sc_compile_b(&tab, &blob, &blob_len);
    if (rc == 0 || tab.err_line != 2) {
        printf("FAIL: compiler error line mismatch (%d != 2)\n", tab.err_line);
        return 9;
    }

    /* Test 8: Disassembly formatting */
    char dis_buf[64];
    sc_disasm_armv6_word(0xE2810002u, dis_buf, sizeof(dis_buf)); /* add r0, r1, #2 */
    if (strstr(dis_buf, "add") == NULL) {
        printf("FAIL: disasm opcode mismatch (%s)\n", dis_buf);
        return 10;
    }

    /* Test 9: Vim Navigation, Insert Mode, and Editing */
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.screen_cols = 80;
    g_sc.screen_rows = 25;
    g_sc.ntabs = 1;
    g_sc.cur_tab = 0;
    sc_tab_init_empty(&g_sc.tabs[0], "scratch.b");

    /* Switch to insert mode with 'i', type "local a = 10", press Enter */
    sc_handle_key('i');
    if (g_sc.mode != SC_MODE_INSERT) {
        printf("FAIL: switch to insert mode\n");
        return 11;
    }
    const char *typed = "local a = 10";
    for (size_t k = 0; k < strlen(typed); k++)
        sc_handle_key(typed[k]);
    sc_handle_key('\n');
    sc_handle_key(0x1B); /* Esc -> back to normal */
    if (g_sc.mode != SC_MODE_NORMAL) {
        printf("FAIL: Esc to normal mode\n");
        return 12;
    }

    /* Test 10: Drawing functions */
    sc_draw_tabs();
    sc_draw_editor();

    sc_tab_free(&g_sc.tabs[0]);
    sc_tab_free(&tab);
    printf("shell_code tests passed: 10/10 checks ok\n");
    return 0;
}
