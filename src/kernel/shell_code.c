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
#include <math.h>

#include "bm/bmlc.h"
#include "bm/bmnative.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef SHELL_CODE_HOST_TEST
#include "gfx/console.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "drivers/fb.h"
#include "drivers/timer.h"
#include "input.h"
#include "usb/hid.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "monitor_fs.h"
#include "script/luavm.h"
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
    int        sel_active;
    int        sel_anchor_cy;
    int        sel_anchor_cx;
    char       status[256];
} sc_tab_t;

enum {
    MENU_RESUME = 0,
    MENU_OPEN,
    MENU_SAVE,
    MENU_SAVE_AS,
    MENU_NEW,
    MENU_NEW_LUA,
    MENU_CHECK,
    MENU_RUN,
    MENU_DISASM,
    MENU_NEXT_TAB,
    MENU_CLOSE_TAB,
    MENU_EXIT,
    MENU_COUNT
};

static const char *const g_menu_labels[MENU_COUNT] = {
    "Resume Editing",
    "Open File...     (Ctrl+O)",
    "Save             (Ctrl+S)",
    "Save As...",
    "New .b File      (Ctrl+N)",
    "New .lua File",
    "Check Syntax     (F6)",
    "Compile & Run    (F5)",
    "Disassemble .b   (F7)",
    "Next Tab         (Ctrl+T)",
    "Close Tab",
    "Exit Shell Code"
};

#ifndef FAT_NAME_MAX
#define FAT_NAME_MAX 128
#endif

typedef struct {
    char     name[FAT_NAME_MAX];
    int      is_dir;
    uint32_t size;
} sc_dirent_t;

#define SC_MAX_DIRENTS 128

typedef struct {
    int         active;
    char        cur_dir[128];
    sc_dirent_t entries[SC_MAX_DIRENTS];
    int         nentries;
    int         sel;
    int         top;
} sc_picker_t;

static sc_picker_t g_picker;

typedef struct {
    sc_tab_t tabs[SC_MAX_TABS];
    int      ntabs;
    int      cur_tab;
    int      show_menu;
    int      menu_sel;
    int      quit_requested;
    int      screen_cols;
    int      screen_rows;
    char    *clipboard;
    size_t   clipboard_len;
} sc_state_t;

static sc_state_t g_sc;

static int g_mock_ctrl = 0, g_mock_shift = 0, g_mock_alt = 0;

#ifndef SHELL_CODE_HOST_TEST
static inline int sc_ctrl_held(void) {
    return g_mock_ctrl || hid_usage_held(0xE0) || hid_usage_held(0xE4);
}
static inline int sc_shift_held(void) {
    return g_mock_shift || hid_usage_held(0xE1) || hid_usage_held(0xE5);
}
static inline int sc_alt_held(void) {
    return g_mock_alt || hid_usage_held(0xE2) || hid_usage_held(0xE6);
}
#else
static inline int sc_ctrl_held(void) { return g_mock_ctrl; }
static inline int sc_shift_held(void) { return g_mock_shift; }
static inline int sc_alt_held(void) { return g_mock_alt; }
#endif

/* ---- Path Normalization (/code-b/, /code-lua/) ---- */

static void sc_normalize_path(char *dest, size_t dest_cap, const char *src)
{
    if (!src || !*src) {
        snprintf(dest, dest_cap, "/code-b/scratch.b");
        return;
    }
    const char *ext = strrchr(src, '.');
    int is_b = (ext && strcmp(ext, ".b") == 0);
    int is_lua = (ext && strcmp(ext, ".lua") == 0);

    if (src[0] == '/') {
        snprintf(dest, dest_cap, "%s", src);
    } else if (is_b) {
        snprintf(dest, dest_cap, "/code-b/%s", src);
    } else if (is_lua) {
        snprintf(dest, dest_cap, "/code-lua/%s", src);
    } else {
        snprintf(dest, dest_cap, "/%s", src);
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
            char msg[256];
            snprintf(msg, sizeof(msg), "Caricato %s (%zu bytes)", path, len);
            snprintf(t->status, sizeof(t->status), "%s", msg);
            return 0;
        }
    }
#endif
    char msg[256];
    snprintf(msg, sizeof(msg), "Nuovo file: %s", path);
    snprintf(t->status, sizeof(t->status), "%s", msg);
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
    "function", "fn", "end", "local", "if", "then", "else", "elseif",
    "while", "do", "for", "return", "break", "struct", "const", "enum",
    "switch", "case", "default", "and", "or", "not"
};
static const char *const b_types[] = {
    "i32", "int", "f32", "bool", "string", "void"
};
static const char *const b_builtins[] = {
    "cls", "pix", "rectfill", "circfill", "spr", "btn", "abs", "min", "max", "sqrt", "floor",
    "line", "rect", "circ", "peek", "poke", "peek2", "poke2", "peek4", "poke4", "memcpy", "memset", "print"
};

#define SC_MAX_DOC_FNS 64
#define SC_FN_NAME_MAX 32

typedef struct {
    char names[SC_MAX_DOC_FNS][SC_FN_NAME_MAX];
    int count;
} sc_doc_fns_t;

static void sc_doc_fns_add(sc_doc_fns_t *out, const char *name, int len)
{
    if (len <= 0 || len >= SC_FN_NAME_MAX || out->count >= SC_MAX_DOC_FNS)
        return;
    char buf[SC_FN_NAME_MAX];
    memcpy(buf, name, (size_t)len);
    buf[len] = '\0';

    for (int k = 0; k < out->count; k++) {
        if (strcmp(out->names[k], buf) == 0)
            return;
    }
    if (strcmp(buf, "i32") == 0 || strcmp(buf, "int") == 0 ||
        strcmp(buf, "bool") == 0 || strcmp(buf, "f32") == 0 ||
        strcmp(buf, "void") == 0 || strcmp(buf, "string") == 0 ||
        strcmp(buf, "if") == 0 || strcmp(buf, "while") == 0 ||
        strcmp(buf, "for") == 0 || strcmp(buf, "return") == 0)
        return;

    snprintf(out->names[out->count], SC_FN_NAME_MAX, "%s", buf);
    out->count++;
}

static void sc_extract_doc_functions(const sc_tab_t *t, sc_doc_fns_t *out)
{
    out->count = 0;
    if (!t || t->nlines <= 0) return;

    for (int y = 0; y < t->nlines && out->count < SC_MAX_DOC_FNS; y++) {
        const sc_line_t *l = &t->lines[y];
        const char *s = l->chars;
        int len = l->len;
        int i = 0;

        while (i < len && out->count < SC_MAX_DOC_FNS) {
            while (i < len && isspace((unsigned char)s[i])) i++;
            if (i >= len) break;

            if (i + 1 < len && ((s[i] == '-' && s[i+1] == '-') || (s[i] == '/' && s[i+1] == '/')))
                break;

            if (!is_ident_start(s[i])) {
                i++;
                continue;
            }

            int t0_start = i;
            while (i < len && is_ident_part(s[i])) i++;
            int t0_len = i - t0_start;

            /* Check "function" or "fn" */
            if ((t0_len == 8 && strncmp(s + t0_start, "function", 8) == 0) ||
                (t0_len == 2 && strncmp(s + t0_start, "fn", 2) == 0)) {
                while (i < len && isspace((unsigned char)s[i])) i++;
                if (i >= len || !is_ident_start(s[i])) continue;

                int t1_start = i;
                while (i < len && is_ident_part(s[i])) i++;
                int t1_len = i - t1_start;

                int p = i;
                while (p < len && isspace((unsigned char)s[p])) p++;
                if (p < len && is_ident_start(s[p])) {
                    /* t1 was return type (e.g. i32, int, void), next is fn name */
                    int t2_start = p;
                    while (p < len && is_ident_part(s[p])) p++;
                    int t2_len = p - t2_start;
                    sc_doc_fns_add(out, s + t2_start, t2_len);
                    i = p;
                } else {
                    sc_doc_fns_add(out, s + t1_start, t1_len);
                }
                continue;
            }

            /* In C syntax: type name( ... ) */
            if (t->syntax == SC_SYN_C) {
                if ((t0_len == 3 && strncmp(s + t0_start, "int", 3) == 0) ||
                    (t0_len == 4 && strncmp(s + t0_start, "void", 4) == 0) ||
                    (t0_len == 4 && strncmp(s + t0_start, "char", 4) == 0) ||
                    (t0_len == 5 && strncmp(s + t0_start, "float", 5) == 0) ||
                    (t0_len == 6 && strncmp(s + t0_start, "static", 6) == 0) ||
                    (t0_len == 7 && strncmp(s + t0_start, "uint8_t", 7) == 0) ||
                    (t0_len == 8 && strncmp(s + t0_start, "uint32_t", 8) == 0) ||
                    (t0_len == 7 && strncmp(s + t0_start, "int32_t", 7) == 0)) {
                    while (i < len && (isspace((unsigned char)s[i]) || s[i] == '*')) i++;
                    if (i < len && is_ident_start(s[i])) {
                        int t1_start = i;
                        while (i < len && is_ident_part(s[i])) i++;
                        int t1_len = i - t1_start;
                        int p = i;
                        while (p < len && isspace((unsigned char)s[p])) p++;
                        if (p < len && s[p] == '(') {
                            sc_doc_fns_add(out, s + t1_start, t1_len);
                        }
                    }
                }
            }
        }
    }
}

static int sc_is_doc_function(const sc_doc_fns_t *doc_fns, const char *tok, int len)
{
    if (!doc_fns) return 0;
    for (int i = 0; i < doc_fns->count; i++) {
        if ((int)strlen(doc_fns->names[i]) == len &&
            strncmp(tok, doc_fns->names[i], (size_t)len) == 0)
            return 1;
    }
    return 0;
}

static int sc_get_autocomplete_suggestion(const sc_tab_t *t, char *out_suffix, size_t out_cap,
                                          char *out_full, size_t out_full_cap)
{
    if (out_suffix && out_cap > 0) out_suffix[0] = '\0';
    if (out_full && out_full_cap > 0) out_full[0] = '\0';
    if (!t || t->cy < 0 || t->cy >= t->nlines) return 0;

    const sc_line_t *cur = &t->lines[t->cy];
    if (t->cx <= 0 || t->cx > cur->len) return 0;

    if (t->cx < cur->len && is_ident_part(cur->chars[t->cx]))
        return 0;

    int start = t->cx;
    while (start > 0 && is_ident_part(cur->chars[start - 1]))
        start--;
    int prefix_len = t->cx - start;
    if (prefix_len < 2) return 0;

    char prefix[64];
    if (prefix_len >= (int)sizeof(prefix)) return 0;
    memcpy(prefix, cur->chars + start, (size_t)prefix_len);
    prefix[prefix_len] = '\0';

    /* 1. Document functions match first */
    sc_doc_fns_t doc_fns;
    sc_extract_doc_functions(t, &doc_fns);
    for (int i = 0; i < doc_fns.count; i++) {
        const char *cand = doc_fns.names[i];
        int clen = (int)strlen(cand);
        if (clen > prefix_len && strncmp(cand, prefix, (size_t)prefix_len) == 0) {
            const char *suf = cand + prefix_len;
            if (out_suffix && out_cap > 0) {
                strncpy(out_suffix, suf, out_cap - 1);
                out_suffix[out_cap - 1] = '\0';
            }
            if (out_full && out_full_cap > 0) {
                strncpy(out_full, cand, out_full_cap - 1);
                out_full[out_full_cap - 1] = '\0';
            }
            return 1;
        }
    }

    /* 2. Language keywords, types, builtins (aligned per syntax) */
    static const char *const kw_b[] = {
        "function", "local", "return", "while", "struct", "break",
        "continue", "switch", "case", "default", "elseif", "else", "then",
        "rectfill", "circfill", "print", "cls", "spr", "btn", "btnp", "line",
        "circ", "rect", "sfx", "music", "time", "sin", "cos", "atan2",
        "peek", "poke", "peek2", "poke2", "peek4", "poke4", "memcpy", "memset",
        "i32", "int", "f32", "bool", "string", "void", "const", "enum",
        NULL
    };
    static const char *const kw_lua[] = {
        "function", "local", "return", "while", "break", "elseif", "else", "then",
        "repeat", "until", "true", "false", "nil",
        "rectfill", "circfill", "print", "cls", "pset", "pix", "spr", "btn", "btnp",
        "line", "circ", "rect", "sfx", "music", "time", "screen", "SCREEN_W", "SCREEN_H",
        "ipairs", "pairs", "tostring", "tonumber", "math", "table", "string",
        NULL
    };
    static const char *const kw_c[] = {
        "int", "void", "char", "float", "double", "static", "const", "return",
        "while", "struct", "typedef", "enum", "switch", "case", "break", "continue",
        "uint32_t", "int32_t", "uint8_t", NULL
    };
    const char *const *kw_candidates = (t->syntax == SC_SYN_LUA) ? kw_lua
                                     : (t->syntax == SC_SYN_C)   ? kw_c
                                                                 : kw_b;

    for (int i = 0; kw_candidates[i]; i++) {
        const char *cand = kw_candidates[i];
        int clen = (int)strlen(cand);
        if (clen > prefix_len && strncmp(cand, prefix, (size_t)prefix_len) == 0) {
            const char *suf = cand + prefix_len;
            if (out_suffix && out_cap > 0) {
                strncpy(out_suffix, suf, out_cap - 1);
                out_suffix[out_cap - 1] = '\0';
            }
            if (out_full && out_full_cap > 0) {
                strncpy(out_full, cand, out_full_cap - 1);
                out_full[out_full_cap - 1] = '\0';
            }
            return 1;
        }
    }

    return 0;
}

static const char *const lua_kw[] = {
    "function", "end", "local", "if", "then", "else", "elseif",
    "while", "do", "for", "in", "return", "break", "repeat", "until", "goto",
    "nil", "and", "or", "not"
};

static const char *const bm_api_kw[] = {
    "cls", "pset", "pix", "rectfill", "circfill", "rect", "circ", "line", "spr",
    "btn", "btnp", "sfx", "music", "time", "print", "screen", "SCREEN_W", "SCREEN_H",
    "abs", "min", "max", "sqrt", "floor", "sin", "cos", "atan2",
    "math", "table", "string", "ipairs", "pairs", "tostring", "tonumber"
};

static const char *const c_kw[] = {
    "int", "void", "char", "float", "double", "static", "const", "return",
    "if", "else", "while", "for", "do", "struct", "typedef", "enum", "switch",
    "case", "break", "continue", "sizeof", "uint32_t", "int32_t", "uint8_t"
};

static int sc_get_selection(const sc_tab_t *t, int *s_cy, int *s_cx, int *e_cy, int *e_cx)
{
    if (!t->sel_active) return 0;
    int a_cy = t->sel_anchor_cy, a_cx = t->sel_anchor_cx;
    int c_cy = t->cy, c_cx = t->cx;
    if (a_cy < c_cy || (a_cy == c_cy && a_cx <= c_cx)) {
        *s_cy = a_cy; *s_cx = a_cx;
        *e_cy = c_cy; *e_cx = c_cx;
    } else {
        *s_cy = c_cy; *s_cx = c_cx;
        *e_cy = a_cy; *e_cx = a_cx;
    }
    return 1;
}

static int sc_is_char_selected(const sc_tab_t *t, int file_line, int col)
{
    int s_cy, s_cx, e_cy, e_cx;
    if (!sc_get_selection(t, &s_cy, &s_cx, &e_cy, &e_cx))
        return 0;
    if (file_line < s_cy || file_line > e_cy)
        return 0;
    if (s_cy == e_cy)
        return (col >= s_cx && col < e_cx);
    if (file_line == s_cy)
        return (col >= s_cx);
    if (file_line == e_cy)
        return (col < e_cx);
    return 1;
}

/* Formats line content with syntax highlighting and explicit inverse block cursor */
static void sc_format_line(const sc_tab_t *t, int file_line, int is_cursor_line,
                           int cursor_cx, int col_off, int max_cols,
                           const sc_doc_fns_t *doc_fns, const char *sug_suffix,
                           char *out, size_t max_len)
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
            if (sug_suffix && sug_suffix[0]) {
                int sug_len = (int)strlen(sug_suffix);
                /* Render active block cursor over first ghost character */
                pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m", sug_suffix[0]);
                col++;
                /* Render ghost suffix remainder in dim gray */
                for (int si = 1; si < sug_len && col < end_col; si++) {
                    pos += snprintf(out + pos, max_len - pos, "\x1b[90m%c\x1b[0m", sug_suffix[si]);
                    col++;
                }
            } else {
                /* Render active block cursor: reverse video / negative */
                char c = (col < len) ? s[col] : ' ';
                pos += snprintf(out + pos, max_len - pos, "\x1b[7;1;97m%c\x1b[0m", c);
                col++;
            }
            continue;
        }
        if (sc_is_char_selected(t, file_line, col)) {
            char c = (col < len) ? s[col] : ' ';
            pos += snprintf(out + pos, max_len - pos, "\x1b[44;97m%c\x1b[0m", c);
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
                else if (doc_fns && sc_is_doc_function(doc_fns, s + start, tok_len))
                    color = "\x1b[1;94m";       /* bright blue for user-defined functions */
            } else if (t->syntax == SC_SYN_LUA) {
                if (sc_match_kw(s + start, tok_len, lua_kw, sizeof(lua_kw)/sizeof(lua_kw[0])))
                    color = "\x1b[1;93m";
                else if (sc_match_kw(s + start, tok_len, bm_api_kw, sizeof(bm_api_kw)/sizeof(bm_api_kw[0])))
                    color = "\x1b[1;96m";
                else if ((tok_len == 4 && strncmp(s + start, "true", 4) == 0) ||
                         (tok_len == 5 && strncmp(s + start, "false", 5) == 0))
                    color = "\x1b[35m";
                else if (doc_fns && sc_is_doc_function(doc_fns, s + start, tok_len))
                    color = "\x1b[1;94m";
            } else if (t->syntax == SC_SYN_C) {
                if (sc_match_kw(s + start, tok_len, c_kw, sizeof(c_kw)/sizeof(c_kw[0])))
                    color = "\x1b[1;96m";
                else if (doc_fns && sc_is_doc_function(doc_fns, s + start, tok_len))
                    color = "\x1b[1;94m";
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

    sc_doc_fns_t doc_fns;
    sc_extract_doc_functions(t, &doc_fns);

    char sug_suffix[64] = {0};
    int has_sug = sc_get_autocomplete_suggestion(t, sug_suffix, sizeof(sug_suffix), NULL, 0);

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
            sc_format_line(t, file_line, is_cur, t->cx, t->col_off, text_cols,
                           &doc_fns, (is_cur && has_sug) ? sug_suffix : NULL,
                           text_buf, sizeof(text_buf));
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

/* ---- File Picker & Directory Browser ---- */

static int sc_dirent_cmp(const void *a, const void *b)
{
    const sc_dirent_t *ea = (const sc_dirent_t *)a;
    const sc_dirent_t *eb = (const sc_dirent_t *)b;
    if (strcmp(ea->name, "..") == 0) return -1;
    if (strcmp(eb->name, "..") == 0) return 1;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir; /* Dirs first */
    return strcmp(ea->name, eb->name);
}

static void sc_picker_refresh(sc_picker_t *p)
{
    p->nentries = 0;
    p->sel = 0;
    p->top = 0;

    /* Add ".." if not in root "/" */
    if (strcmp(p->cur_dir, "/") != 0 && strcmp(p->cur_dir, "") != 0) {
        snprintf(p->entries[p->nentries].name, sizeof(p->entries[p->nentries].name), "..");
        p->entries[p->nentries].is_dir = 1;
        p->entries[p->nentries].size = 0;
        p->nentries++;
    }

#ifndef SHELL_CODE_HOST_TEST
    fat_dir_t d;
    if (fat_opendir(&d, p->cur_dir) == 0) {
        fat_entry_t e;
        while (p->nentries < SC_MAX_DIRENTS && fat_readdir(&d, &e)) {
            if (e.name[0] == '.') continue;
            snprintf(p->entries[p->nentries].name, sizeof(p->entries[p->nentries].name), "%s", e.name);
            p->entries[p->nentries].is_dir = e.is_dir;
            p->entries[p->nentries].size = e.size;
            p->nentries++;
        }
    }
#else
    if (p->nentries < SC_MAX_DIRENTS) {
        snprintf(p->entries[p->nentries].name, sizeof(p->entries[p->nentries].name), "prova.b");
        p->entries[p->nentries].is_dir = 0;
        p->entries[p->nentries].size = 124;
        p->nentries++;
    }
#endif

    if (p->nentries > 1) {
        qsort(p->entries, (size_t)p->nentries, sizeof(sc_dirent_t), sc_dirent_cmp);
    }
}

static void sc_picker_open(sc_picker_t *p, const char *start_dir)
{
    p->active = 1;
#ifndef SHELL_CODE_HOST_TEST
    fat_mkdirs("/code-b");
#endif
    if (start_dir && *start_dir) {
        snprintf(p->cur_dir, sizeof(p->cur_dir), "%s", start_dir);
    } else {
        snprintf(p->cur_dir, sizeof(p->cur_dir), "/code-b");
    }
    sc_picker_refresh(p);
}

static void sc_draw_picker(const sc_picker_t *p)
{
    int w = 50;
    if (w > g_sc.screen_cols - 4) w = g_sc.screen_cols - 4;
    int page = 10;
    if (page > g_sc.screen_rows - 8) page = g_sc.screen_rows - 8;
    if (page < 3) page = 3;
    int h = page + 4;
    int top = (g_sc.screen_rows - h) / 2;
    if (top < 1) top = 1;
    int left = (g_sc.screen_cols - w) / 2;
    if (left < 1) left = 1;

    char buf[512];

    /* Header */
    char title[64];
    snprintf(title, sizeof(title), " Apri: %.24s ", p->cur_dir);
    int tlen = (int)strlen(title);
    int rem = w - 4 - tlen;
    if (rem < 0) rem = 0;
    int written = snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xda\xc4%s", top, left, title);
    for (int k = 0; k < rem && written + 1 < (int)sizeof(buf); k++) buf[written++] = (char)0xC4;
    snprintf(buf + written, sizeof(buf) - (size_t)written, "\xbf\x1b[0m");
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif

    /* Empty line */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3%*s\xb3\x1b[0m", top + 1, left, w - 2, "");
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif

    /* Items */
    for (int i = 0; i < page; i++) {
        int r = top + 2 + i;
        int idx = p->top + i;
        if (idx < p->nentries) {
            const sc_dirent_t *de = &p->entries[idx];
            char item_str[64];
            if (de->is_dir) {
                if (strcmp(de->name, "..") == 0)
                    snprintf(item_str, sizeof(item_str), "[..] (Cartella superiore)");
                else
                    snprintf(item_str, sizeof(item_str), "<DIR> %s/", de->name);
            } else {
                snprintf(item_str, sizeof(item_str), "%-24.24s %6lu B", de->name, (unsigned long)de->size);
            }
            int inner_w = w - 8;
            if (idx == p->sel) {
                snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3\x1b[1;30;47m > %-*.*s < \x1b[1;97;44m\xb3\x1b[0m",
                         r, left, inner_w, inner_w, item_str);
            } else {
                snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3   %-*.*s   \xb3\x1b[0m",
                         r, left, inner_w, inner_w, item_str);
            }
        } else {
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xb3%*s\xb3\x1b[0m", r, left, w - 2, "");
        }
#ifndef SHELL_CODE_HOST_TEST
        console_write(buf);
#endif
    }

    /* Footer */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;97;44m\xc0\xc4 [Invio] Apri  [Esc] Annulla ", top + 2 + page, left);
    written = (int)strlen(buf);
    int foot_rem = left + w - 1 - (left + 30);
    for (int k = 0; k < foot_rem && written + 1 < (int)sizeof(buf); k++) buf[written++] = (char)0xC4;
    snprintf(buf + written, sizeof(buf) - (size_t)written, "\xd9\x1b[0m");
#ifndef SHELL_CODE_HOST_TEST
    console_write(buf);
#endif
}

static void sc_picker_handle_key(sc_picker_t *p, int c)
{
    int page = 10;
    if (page > g_sc.screen_rows - 8) page = g_sc.screen_rows - 8;
    if (page < 3) page = 3;

    if (c == 0x1B || c == 'q' || c == 'Q') {
        p->active = 0;
        return;
    }
    if (c == HID_KEY_UP || c == 'k') {
        if (p->sel > 0) {
            p->sel--;
            if (p->sel < p->top) p->top = p->sel;
        }
        return;
    }
    if (c == HID_KEY_DOWN || c == 'j') {
        if (p->sel + 1 < p->nentries) {
            p->sel++;
            if (p->sel >= p->top + page) p->top = p->sel - page + 1;
        }
        return;
    }
    if (c == HID_KEY_PGUP) {
        p->sel -= page;
        if (p->sel < 0) p->sel = 0;
        p->top = p->sel;
        return;
    }
    if (c == HID_KEY_PGDN) {
        p->sel += page;
        if (p->sel >= p->nentries) p->sel = (p->nentries > 0) ? p->nentries - 1 : 0;
        if (p->sel >= p->top + page) p->top = p->sel - page + 1;
        return;
    }
    if (c == '\n' || c == '\r') {
        if (p->nentries == 0) return;
        const sc_dirent_t *de = &p->entries[p->sel];
        if (strcmp(de->name, "..") == 0) {
            /* Parent directory */
            char *last = strrchr(p->cur_dir, '/');
            if (last && last == p->cur_dir) {
                p->cur_dir[1] = '\0';
            } else if (last) {
                *last = '\0';
            } else {
                snprintf(p->cur_dir, sizeof(p->cur_dir), "/");
            }
            sc_picker_refresh(p);
            return;
        }
        if (de->is_dir) {
            /* Enter directory */
            if (strcmp(p->cur_dir, "/") == 0) {
                snprintf(p->cur_dir, sizeof(p->cur_dir), "/%s", de->name);
            } else {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "%s/%s", p->cur_dir, de->name);
                snprintf(p->cur_dir, sizeof(p->cur_dir), "%.127s", tmp);
            }
            sc_picker_refresh(p);
            return;
        }

        /* File selected: open it! */
        char full_path[256];
        if (strcmp(p->cur_dir, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", de->name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", p->cur_dir, de->name);
        }
        p->active = 0;

        sc_tab_t *cur = &g_sc.tabs[g_sc.cur_tab];
        if (cur->nlines == 1 && cur->lines[0].len == 0 && !cur->dirty) {
            sc_tab_load(cur, full_path);
        } else if (g_sc.ntabs < SC_MAX_TABS) {
            int new_idx = g_sc.ntabs++;
            sc_tab_load(&g_sc.tabs[new_idx], full_path);
            g_sc.cur_tab = new_idx;
        } else {
            sc_tab_load(cur, full_path);
        }
        return;
    }
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

static int g_shell_did_draw_gfx = 0;

#ifndef SHELL_CODE_HOST_TEST
static uint32_t sc_fb_rgb(const framebuffer_t *fb, uint32_t rgb)
{
    return fb_color(fb, (uint8_t)((rgb >> 16) & 0xFFu),
                        (uint8_t)((rgb >> 8) & 0xFFu),
                        (uint8_t)(rgb & 0xFFu));
}

static void sc_fb_line(framebuffer_t *fb, int x0, int y0, int x1, int y1, uint32_t col)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if ((uint32_t)x0 < fb->width && (uint32_t)y0 < fb->height)
            fb_putpixel(fb, (uint32_t)x0, (uint32_t)y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void sc_fb_circ(framebuffer_t *fb, int cx, int cy, int r, uint32_t col, int fill)
{
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        if (fill) {
            int lx0 = cx - x, lx1 = cx + x, ux0 = cx - y, ux1 = cx + y;
            for (int px = lx0; px <= lx1; px++) {
                if ((uint32_t)px < fb->width) {
                    if ((uint32_t)(cy + y) < fb->height) fb_putpixel(fb, (uint32_t)px, (uint32_t)(cy + y), col);
                    if ((uint32_t)(cy - y) < fb->height) fb_putpixel(fb, (uint32_t)px, (uint32_t)(cy - y), col);
                }
            }
            for (int px = ux0; px <= ux1; px++) {
                if ((uint32_t)px < fb->width) {
                    if ((uint32_t)(cy + x) < fb->height) fb_putpixel(fb, (uint32_t)px, (uint32_t)(cy + x), col);
                    if ((uint32_t)(cy - x) < fb->height) fb_putpixel(fb, (uint32_t)px, (uint32_t)(cy - x), col);
                }
            }
        } else {
            int pts[8][2] = {
                {cx + x, cy + y}, {cx - x, cy + y}, {cx + x, cy - y}, {cx - x, cy - y},
                {cx + y, cy + x}, {cx - y, cy + x}, {cx + y, cy - x}, {cx - y, cy - x}
            };
            for (int i = 0; i < 8; i++) {
                if ((uint32_t)pts[i][0] < fb->width && (uint32_t)pts[i][1] < fb->height)
                    fb_putpixel(fb, (uint32_t)pts[i][0], (uint32_t)pts[i][1], col);
            }
        }
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static uint32_t g_sc_prev_btns = 0;

static int32_t shell_code_host_dispatch(int32_t host_id, const int32_t *args, int32_t nargs)
{
    framebuffer_t *fb = console_framebuffer();
    if (!fb) return 0;

    switch (host_id) {
    case BMN_HOST_CLS: {
        uint32_t rgb = (nargs > 0) ? (uint32_t)args[0] : 0;
        fb_fill_rect(fb, 0, 0, fb->width, fb->height, sc_fb_rgb(fb, rgb));
        g_shell_did_draw_gfx = 1;
        return 0;
    }
    case BMN_HOST_PIX: {
        if (nargs >= 3) {
            uint32_t x = (uint32_t)args[0];
            uint32_t y = (uint32_t)args[1];
            uint32_t col = sc_fb_rgb(fb, (uint32_t)args[2]);
            if (x < fb->width && y < fb->height)
                fb_putpixel(fb, x, y, col);
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_RECTFILL: {
        if (nargs >= 5) {
            int x = args[0], y = args[1], w = args[2], h = args[3];
            if (w > 0 && h > 0 && x < (int)fb->width && y < (int)fb->height) {
                if (x < 0) { w += x; x = 0; }
                if (y < 0) { h += y; y = 0; }
                if (x + w > (int)fb->width) w = (int)fb->width - x;
                if (y + h > (int)fb->height) h = (int)fb->height - y;
                if (w > 0 && h > 0)
                    fb_fill_rect(fb, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                                 sc_fb_rgb(fb, (uint32_t)args[4]));
            }
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_RECT: {
        if (nargs >= 5) {
            gfx_rect(fb, args[0], args[1], args[2], args[3], sc_fb_rgb(fb, (uint32_t)args[4]));
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_LINE: {
        if (nargs >= 5) {
            sc_fb_line(fb, args[0], args[1], args[2], args[3], sc_fb_rgb(fb, (uint32_t)args[4]));
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_CIRC: {
        if (nargs >= 4) {
            sc_fb_circ(fb, args[0], args[1], args[2], sc_fb_rgb(fb, (uint32_t)args[3]), 0);
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_CIRCFILL: {
        if (nargs >= 4) {
            sc_fb_circ(fb, args[0], args[1], args[2], sc_fb_rgb(fb, (uint32_t)args[3]), 1);
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_SPR: {
        if (nargs >= 3) {
            fb_fill_rect(fb, (uint32_t)args[1], (uint32_t)args[2], 8, 8, sc_fb_rgb(fb, 0xFF00FF));
            g_shell_did_draw_gfx = 1;
        }
        return 0;
    }
    case BMN_HOST_PRINT: {
        if (nargs >= 1) {
            const char *s = bmnative_get_string(args[0]);
            int x = (nargs >= 2) ? args[1] : 0;
            int y = (nargs >= 3) ? args[2] : 0;
            uint32_t col = (nargs >= 4) ? sc_fb_rgb(fb, (uint32_t)args[3]) : sc_fb_rgb(fb, 0xFFFFFF);
            int end_x = gfx_text(fb, &font_console_8x16, x, y, s ? s : "", col, sc_fb_rgb(fb, 0));
            g_shell_did_draw_gfx = 1;
            return end_x;
        }
        return 0;
    }
    case BMN_HOST_BTN: {
        uint32_t b = (nargs > 0) ? (uint32_t)args[0] : 0;
        uint32_t btns = hid_buttons();
        return (btns & (1u << b)) ? 1 : 0;
    }
    case BMN_HOST_BTNP: {
        uint32_t b = (nargs > 0) ? (uint32_t)args[0] : 0;
        uint32_t btns = hid_buttons();
        uint32_t pressed = btns & ~g_sc_prev_btns;
        return (pressed & (1u << b)) ? 1 : 0;
    }
    case BMN_HOST_TIME: {
        float sec = (float)timer_ticks() / 1000000.0f;
        uint32_t bits = 0;
        memcpy(&bits, &sec, 4);
        return (int32_t)bits;
    }
    case BMN_HOST_SIN: {
        float a = 0.0f;
        if (nargs >= 1) memcpy(&a, &args[0], 4);
        float r = sinf(a);
        uint32_t bits = 0;
        memcpy(&bits, &r, 4);
        return (int32_t)bits;
    }
    case BMN_HOST_COS: {
        float a = 0.0f;
        if (nargs >= 1) memcpy(&a, &args[0], 4);
        float r = cosf(a);
        uint32_t bits = 0;
        memcpy(&bits, &r, 4);
        return (int32_t)bits;
    }
    case BMN_HOST_ATAN2: {
        float y = 0.0f, x = 0.0f;
        if (nargs >= 2) { memcpy(&y, &args[0], 4); memcpy(&x, &args[1], 4); }
        float r = atan2f(y, x);
        uint32_t bits = 0;
        memcpy(&bits, &r, 4);
        return (int32_t)bits;
    }
    default:
        break;
    }
    return 0;
}

static void sc_find_fn_name_on_line(const char *s, char *out_name, size_t out_cap)
{
    if (out_cap > 0) out_name[0] = '\0';
    if (!s) return;
    int len = (int)strlen(s);
    int i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    if (i >= len) return;

    const char *p = strstr(s, "function ");
    int skip = 9;
    if (!p) {
        p = strstr(s, "fn ");
        skip = 3;
    }
    if (p) {
        p += skip;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p || !is_ident_start(*p)) return;
        const char *t1 = p;
        while (*p && is_ident_part(*p)) p++;
        int t1_len = (int)(p - t1);
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p && is_ident_start(*p)) {
            /* t1 was return type (e.g. i32, int, void), next is fn name */
            const char *t2 = p;
            while (*p && is_ident_part(*p)) p++;
            int t2_len = (int)(p - t2);
            if ((size_t)t2_len < out_cap) {
                memcpy(out_name, t2, (size_t)t2_len);
                out_name[t2_len] = '\0';
            }
        } else {
            if ((size_t)t1_len < out_cap) {
                memcpy(out_name, t1, (size_t)t1_len);
                out_name[t1_len] = '\0';
            }
        }
    }
}
#endif

static lua_State *sc_lua_newstate(void)
{
#ifndef SHELL_CODE_HOST_TEST
    return luavm_newstate();
#else
    return luaL_newstate();
#endif
}

static int sc_extract_lua_err_line(const char *msg)
{
    if (!msg) return 0;
    const char *p = strstr(msg, "main.lua:");
    if (p) return atoi(p + 9);
    const char *c1 = strchr(msg, ':');
    if (c1 && isdigit((unsigned char)c1[1])) return atoi(c1 + 1);
    return 0;
}

static void sc_jump_to_err_line(sc_tab_t *t, int line)
{
    t->err_line = line;
    if (t->err_line > 0 && t->err_line <= t->nlines) {
        t->cy = t->err_line - 1;
        t->cx = 0;
        sc_scroll_to_cursor(t);
    }
}

static int sc_compile_lua(sc_tab_t *t)
{
    size_t src_len = 0;
    char *src = sc_tab_get_text(t, &src_len);
    if (!src) {
        snprintf(t->status, sizeof(t->status), "Errore memoria");
        return -1;
    }

    lua_State *L = sc_lua_newstate();
    if (!L) {
        free(src);
        snprintf(t->status, sizeof(t->status), "Errore memoria Lua");
        return -1;
    }

    int rc = luaL_loadbufferx(L, src, src_len, "=main.lua", "t");
    free(src);

    if (rc != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        int ln = sc_extract_lua_err_line(msg);
        sc_jump_to_err_line(t, ln);
        snprintf(t->status, sizeof(t->status), "Errore Lua: %s", msg ? msg : "syntax error");
        lua_close(L);
        return -1;
    }

    lua_close(L);
    t->err_line = 0;
    snprintf(t->status, sizeof(t->status), "Lua OK: %zu bytes", src_len);
    return 0;
}

static int sc_lua_cls(lua_State *L)
{
    int32_t c = (int32_t)luaL_optinteger(L, 1, 0);
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_CLS, &c, 1);
#else
    (void)c;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_pset(lua_State *L)
{
    int32_t a[3] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_optinteger(L, 3, 0xFFFFFF)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_PIX, a, 3);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_rectfill(lua_State *L)
{
    int32_t a[5] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3),
        (int32_t)luaL_checkinteger(L, 4),
        (int32_t)luaL_optinteger(L, 5, 0xFFFFFF)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_RECTFILL, a, 5);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_rect(lua_State *L)
{
    int32_t a[5] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3),
        (int32_t)luaL_checkinteger(L, 4),
        (int32_t)luaL_optinteger(L, 5, 0xFFFFFF)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_RECT, a, 5);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_line(lua_State *L)
{
    int32_t a[5] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3),
        (int32_t)luaL_checkinteger(L, 4),
        (int32_t)luaL_optinteger(L, 5, 0xFFFFFF)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_LINE, a, 5);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_circ(lua_State *L)
{
    int32_t a[4] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3),
        (int32_t)luaL_optinteger(L, 4, 0xFFFFFF)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_CIRC, a, 4);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_circfill(lua_State *L)
{
    int32_t a[4] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3),
        (int32_t)luaL_optinteger(L, 4, 0xFFFFFF)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_CIRCFILL, a, 4);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_spr(lua_State *L)
{
    int32_t a[3] = {
        (int32_t)luaL_checkinteger(L, 1),
        (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3)
    };
#ifndef SHELL_CODE_HOST_TEST
    shell_code_host_dispatch(BMN_HOST_SPR, a, 3);
#else
    (void)a;
    g_shell_did_draw_gfx = 1;
#endif
    return 0;
}

static int sc_lua_print(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, 0);
    int y = (int)luaL_optinteger(L, 3, 0);
    uint32_t rgb = (uint32_t)luaL_optinteger(L, 4, 0xFFFFFF);
    int end_x = x;
#ifndef SHELL_CODE_HOST_TEST
    framebuffer_t *fb = console_framebuffer();
    if (fb) {
        end_x = gfx_text(fb, &font_console_8x16, x, y, s, sc_fb_rgb(fb, rgb), sc_fb_rgb(fb, 0));
        g_shell_did_draw_gfx = 1;
    }
#else
    (void)s; (void)y; (void)rgb;
    g_shell_did_draw_gfx = 1;
#endif
    lua_pushinteger(L, end_x);
    return 1;
}

static int sc_lua_btn(lua_State *L)
{
    int32_t b = (int32_t)luaL_optinteger(L, 1, 0);
#ifndef SHELL_CODE_HOST_TEST
    lua_pushboolean(L, shell_code_host_dispatch(BMN_HOST_BTN, &b, 1) != 0);
#else
    (void)b;
    lua_pushboolean(L, 0);
#endif
    return 1;
}

static int sc_lua_btnp(lua_State *L)
{
    int32_t b = (int32_t)luaL_optinteger(L, 1, 0);
#ifndef SHELL_CODE_HOST_TEST
    lua_pushboolean(L, shell_code_host_dispatch(BMN_HOST_BTNP, &b, 1) != 0);
#else
    (void)b;
    lua_pushboolean(L, 0);
#endif
    return 1;
}

static int sc_lua_time(lua_State *L)
{
#ifndef SHELL_CODE_HOST_TEST
    lua_pushnumber(L, (lua_Number)timer_ticks() / 1000000.0);
#else
    lua_pushnumber(L, 0.0);
#endif
    return 1;
}

static void sc_lua_hook_limit(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    luaL_error(L, "instruction limit");
}

static int sc_run_lua(sc_tab_t *t)
{
    size_t src_len = 0;
    char *src = sc_tab_get_text(t, &src_len);
    if (!src) {
        snprintf(t->status, sizeof(t->status), "Errore memoria");
        return -1;
    }

    lua_State *L = sc_lua_newstate();
    if (!L) {
        free(src);
        snprintf(t->status, sizeof(t->status), "Errore memoria Lua");
        return -1;
    }

    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
        { LUA_COLIBNAME, luaopen_coroutine }, { LUA_UTF8LIBNAME, luaopen_utf8 },
        { NULL, NULL },
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    static const char *const removed[] = { "dofile", "loadfile", "load", NULL };
    for (const char *const *r = removed; *r; r++) {
        lua_pushnil(L);
        lua_setglobal(L, *r);
    }
    static const luaL_Reg bm_funcs[] = {
        { "cls", sc_lua_cls }, { "pset", sc_lua_pset }, { "pix", sc_lua_pset },
        { "rectfill", sc_lua_rectfill }, { "rect", sc_lua_rect },
        { "line", sc_lua_line }, { "circ", sc_lua_circ }, { "circfill", sc_lua_circfill },
        { "spr", sc_lua_spr }, { "print", sc_lua_print },
        { "btn", sc_lua_btn }, { "btnp", sc_lua_btnp }, { "time", sc_lua_time },
        { NULL, NULL }
    };
    lua_pushglobaltable(L);
    luaL_setfuncs(L, bm_funcs, 0);
    lua_pop(L, 1);

    int sw = 640, sh = 360;
#ifndef SHELL_CODE_HOST_TEST
    framebuffer_t *fb = console_framebuffer();
    if (fb) { sw = (int)fb->width; sh = (int)fb->height; }
#endif
    lua_pushinteger(L, sw);
    lua_setglobal(L, "SCREEN_W");
    lua_pushinteger(L, sh);
    lua_setglobal(L, "SCREEN_H");

    int rc = luaL_loadbufferx(L, src, src_len, "=main.lua", "t");
    free(src);
    if (rc != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
        snprintf(t->status, sizeof(t->status), "Errore Lua: %s", msg ? msg : "syntax error");
        lua_close(L);
        return -1;
    }

    g_shell_did_draw_gfx = 0;
#ifndef SHELL_CODE_HOST_TEST
    g_sc_prev_btns = hid_buttons();
    uint32_t t0 = timer_ticks();
#endif
    lua_sethook(L, sc_lua_hook_limit, LUA_MASKCOUNT, 5000000);
    rc = lua_pcall(L, 0, 1, 0);
#ifndef SHELL_CODE_HOST_TEST
    uint32_t us = timer_ticks() - t0;
#else
    uint32_t us = 0;
#endif
    if (rc != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
        snprintf(t->status, sizeof(t->status), "Errore runtime Lua: %s", msg ? msg : "error");
        lua_close(L);
        return -1;
    }

    char ret_buf[96] = {0};
    if (!lua_isnil(L, -1)) {
        const char *rs = luaL_tolstring(L, -1, NULL);
        if (rs) snprintf(ret_buf, sizeof(ret_buf), "%s", rs);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getglobal(L, "_update");
    int has_upd = lua_isfunction(L, -1);
    lua_pop(L, 1);
    lua_getglobal(L, "_draw");
    int has_drw = lua_isfunction(L, -1);
    lua_pop(L, 1);

    if (has_upd || has_drw) {
        lua_getglobal(L, "_init");
        if (lua_isfunction(L, -1)) {
            lua_sethook(L, sc_lua_hook_limit, LUA_MASKCOUNT, 5000000);
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                const char *msg = lua_tostring(L, -1);
                sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
                snprintf(t->status, sizeof(t->status), "Errore _init: %s", msg ? msg : "error");
                lua_close(L);
                return -1;
            }
        } else {
            lua_pop(L, 1);
        }
#ifndef SHELL_CODE_HOST_TEST
        uint32_t frames = 0;
        for (;;) {
            int k = input_key();
            if (k == 0x1B || k == 'q' || k == 'Q') break;
            if (has_upd) {
                lua_sethook(L, sc_lua_hook_limit, LUA_MASKCOUNT, 5000000);
                lua_getglobal(L, "_update");
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    const char *msg = lua_tostring(L, -1);
                    sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
                    snprintf(t->status, sizeof(t->status), "Errore _update: %s", msg ? msg : "error");
                    lua_close(L);
                    console_clear();
                    return -1;
                }
            }
            if (has_drw) {
                lua_sethook(L, sc_lua_hook_limit, LUA_MASKCOUNT, 5000000);
                lua_getglobal(L, "_draw");
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    const char *msg = lua_tostring(L, -1);
                    sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
                    snprintf(t->status, sizeof(t->status), "Errore _draw: %s", msg ? msg : "error");
                    lua_close(L);
                    console_clear();
                    return -1;
                }
            }
            g_sc_prev_btns = hid_buttons();
            frames++;
            timer_delay_ms(16);
        }
        console_clear();
        snprintf(t->status, sizeof(t->status), "Lua loop: %lu frames", (unsigned long)frames);
#else
        if (has_upd) {
            lua_getglobal(L, "_update");
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                const char *msg = lua_tostring(L, -1);
                sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
                snprintf(t->status, sizeof(t->status), "Errore _update: %s", msg ? msg : "error");
                lua_close(L);
                return -1;
            }
        }
        if (has_drw) {
            lua_getglobal(L, "_draw");
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                const char *msg = lua_tostring(L, -1);
                sc_jump_to_err_line(t, sc_extract_lua_err_line(msg));
                snprintf(t->status, sizeof(t->status), "Errore _draw: %s", msg ? msg : "error");
                lua_close(L);
                return -1;
            }
        }
        snprintf(t->status, sizeof(t->status), "Lua loop OK (1 frame test)");
#endif
    } else {
#ifndef SHELL_CODE_HOST_TEST
        if (g_shell_did_draw_gfx) {
            kprintf("\n\x1b[1;92m-- Grafica Lua disegnata (%lu us). Premi un tasto --\x1b[0m\n", (unsigned long)us);
            input_getc();
            console_clear();
        }
#endif
        if (ret_buf[0]) {
            snprintf(t->status, sizeof(t->status), "Lua = %s [%lu us]", ret_buf, (unsigned long)us);
        } else {
            snprintf(t->status, sizeof(t->status), "Lua OK [%lu us]", (unsigned long)us);
        }
    }

    t->err_line = 0;
    lua_close(L);
    return 0;
}

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

    int init_idx = bmnative_find(m, "_init");
    int upd_idx = bmnative_find(m, "_update");
    int drw_idx = bmnative_find(m, "_draw");
    if (upd_idx >= 0 || drw_idx >= 0) {
        int32_t b_state[256];
        memset(b_state, 0, sizeof(b_state));
        bmnative_buf sbuf = { (uint32_t *)b_state, 256 };
        bmnative_host_fn prev_host = bmnative_get_host_fn();
        bmnative_set_host_fn(shell_code_host_dispatch);
        g_sc_prev_btns = hid_buttons();
        int err = BMN_OK;
        int32_t ret = 0;
        if (init_idx >= 0) {
            const bmnative_fn *fi = &m->fn[init_idx];
            if (fi->nparams == 1 && fi->ptype[0] == BMN_ARR_I32)
                err = bmnative_call_buf(m, init_idx, NULL, &sbuf, 1, &ret);
            else if (fi->nparams == 0)
                err = bmnative_call(m, init_idx, NULL, 0, &ret);
        }
        uint32_t frames = 0;
        while (err == BMN_OK) {
            int k = input_key();
            if (k == 0x1B || k == 'q' || k == 'Q') break;
            if (upd_idx >= 0) {
                const bmnative_fn *fu = &m->fn[upd_idx];
                if (fu->nparams == 1 && fu->ptype[0] == BMN_ARR_I32)
                    err = bmnative_call_buf(m, upd_idx, NULL, &sbuf, 1, &ret);
                else if (fu->nparams == 0)
                    err = bmnative_call(m, upd_idx, NULL, 0, &ret);
            }
            if (err == BMN_OK && drw_idx >= 0) {
                const bmnative_fn *fd = &m->fn[drw_idx];
                if (fd->nparams == 1 && fd->ptype[0] == BMN_ARR_I32)
                    err = bmnative_call_buf(m, drw_idx, NULL, &sbuf, 1, &ret);
                else if (fd->nparams == 0)
                    err = bmnative_call(m, drw_idx, NULL, 0, &ret);
            }
            g_sc_prev_btns = hid_buttons();
            frames++;
            timer_delay_ms(16);
        }
        bmnative_set_host_fn(prev_host);
        console_clear();
        if (err == BMN_OK)
            snprintf(t->status, sizeof(t->status), ".b loop: %lu frames", (unsigned long)frames);
        else
            snprintf(t->status, sizeof(t->status), "Trap runtime (%d) nel loop .b", err);
        bmnative_free(m);
        free(blob);
        return (err == BMN_OK) ? 0 : -1;
    }

    int fn_idx = -1;
    if (t->cy < t->nlines) {
        char fname[64];
        sc_find_fn_name_on_line(t->lines[t->cy].chars, fname, sizeof(fname));
        if (fname[0])
            fn_idx = bmnative_find(m, fname);
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
    g_sc_prev_btns = hid_buttons();
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

static int sc_check_syntax(sc_tab_t *t)
{
    if (t->syntax == SC_SYN_B) {
        uint8_t *blob = NULL;
        size_t blob_len = 0;
        int rc = sc_compile_b(t, &blob, &blob_len);
        free(blob);
        return rc;
    }
    if (t->syntax == SC_SYN_LUA)
        return sc_compile_lua(t);
    snprintf(t->status, sizeof(t->status), "Controllo sintassi solo per .b e .lua");
    return 0;
}

static int sc_run_active(sc_tab_t *t, const char *arg_str)
{
    if (t->syntax == SC_SYN_B)
        return sc_run_b(t, arg_str);
    if (t->syntax == SC_SYN_LUA)
        return sc_run_lua(t);
    snprintf(t->status, sizeof(t->status), "Esecuzione disponibile per file .b e .lua");
    return -1;
}

/* ---- Disassembly Viewer ---- */

static void sc_disasm_viewer(sc_tab_t *t)
{
    if (t->syntax != SC_SYN_B) {
        snprintf(t->status, sizeof(t->status), "Disassembly disponibile solo per file .b");
        return;
    }
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
            bmnative_disasm_word(w, dstr, sizeof(dstr));
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

static void sc_delete_range(sc_tab_t *t, int s_cy, int s_cx, int e_cy, int e_cx)
{
    if (s_cy == e_cy) {
        sc_line_t *l = &t->lines[s_cy];
        int count = e_cx - s_cx;
        for (int i = 0; i < count; i++)
            sc_line_delete_char(l, s_cx);
        t->cy = s_cy;
        t->cx = s_cx;
        t->dirty = 1;
        return;
    }

    sc_line_t *first = &t->lines[s_cy];
    sc_line_t *last = &t->lines[e_cy];

    /* Truncate first line at s_cx */
    first->len = s_cx;
    first->chars[s_cx] = '\0';

    /* Append remainder of last line */
    if (e_cx < last->len)
        sc_line_append(first, last->chars + e_cx, last->len - e_cx);

    /* Delete intermediate lines from e_cy down to s_cy + 1 */
    for (int r = e_cy; r > s_cy; r--)
        sc_tab_delete_line(t, r);

    t->cy = s_cy;
    t->cx = s_cx;
    t->dirty = 1;
}

static void sc_backspace(sc_tab_t *t)
{
    if (t->sel_active) {
        int s_cy, s_cx, e_cy, e_cx;
        if (sc_get_selection(t, &s_cy, &s_cx, &e_cy, &e_cx)) {
            sc_delete_range(t, s_cy, s_cx, e_cy, e_cx);
            t->sel_active = 0;
            return;
        }
    }
    if (t->cx > 0) {
        sc_line_t *cur = &t->lines[t->cy];
        char prev = cur->chars[t->cx - 1];
        char next = (t->cx < cur->len) ? cur->chars[t->cx] : '\0';
        sc_line_delete_char(cur, t->cx - 1);
        t->cx--;
        /* Auto-delete closing pair if empty */
        if ((prev == '(' && next == ')') || (prev == '[' && next == ']') ||
            (prev == '{' && next == '}') || (prev == '"' && next == '"')) {
            if (t->cx < cur->len)
                sc_line_delete_char(cur, t->cx);
        }
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
    if (t->sel_active) {
        int s_cy, s_cx, e_cy, e_cx;
        if (sc_get_selection(t, &s_cy, &s_cx, &e_cy, &e_cx)) {
            sc_delete_range(t, s_cy, s_cx, e_cy, e_cx);
            t->sel_active = 0;
            return;
        }
    }
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

static void sc_copy_selection_or_line(sc_tab_t *t, int is_cut)
{
    int s_cy, s_cx, e_cy, e_cx;
    free(g_sc.clipboard);
    g_sc.clipboard = NULL;
    g_sc.clipboard_len = 0;

    if (sc_get_selection(t, &s_cy, &s_cx, &e_cy, &e_cx)) {
        size_t est_len = 0;
        for (int r = s_cy; r <= e_cy; r++)
            est_len += (size_t)t->lines[r].len + 2;
        g_sc.clipboard = malloc(est_len + 1);
        if (!g_sc.clipboard) return;

        size_t pos = 0;
        for (int r = s_cy; r <= e_cy; r++) {
            sc_line_t *l = &t->lines[r];
            int c_start = (r == s_cy) ? s_cx : 0;
            int c_end = (r == e_cy) ? e_cx : l->len;
            if (c_end > l->len) c_end = l->len;
            if (c_start < l->len && c_end > c_start) {
                int clen = c_end - c_start;
                memcpy(g_sc.clipboard + pos, l->chars + c_start, (size_t)clen);
                pos += (size_t)clen;
            }
            if (r < e_cy)
                g_sc.clipboard[pos++] = '\n';
        }
        g_sc.clipboard[pos] = '\0';
        g_sc.clipboard_len = pos;

        if (is_cut) {
            sc_delete_range(t, s_cy, s_cx, e_cy, e_cx);
            t->sel_active = 0;
            snprintf(t->status, sizeof(t->status), "Cut %zu bytes", g_sc.clipboard_len);
        } else {
            snprintf(t->status, sizeof(t->status), "Copied %zu bytes", g_sc.clipboard_len);
        }
    } else {
        sc_line_t *cur = &t->lines[t->cy];
        g_sc.clipboard = malloc((size_t)cur->len + 2);
        if (!g_sc.clipboard) return;
        if (cur->len > 0)
            memcpy(g_sc.clipboard, cur->chars, (size_t)cur->len);
        g_sc.clipboard[cur->len] = '\n';
        g_sc.clipboard[cur->len + 1] = '\0';
        g_sc.clipboard_len = (size_t)cur->len + 1;

        if (is_cut) {
            sc_tab_delete_line(t, t->cy);
            if (t->nlines == 0) sc_tab_insert_line(t, 0, "", 0);
            if (t->cy >= t->nlines) t->cy = t->nlines - 1;
            t->cx = 0;
            t->dirty = 1;
            snprintf(t->status, sizeof(t->status), "Cut line");
        } else {
            snprintf(t->status, sizeof(t->status), "Copied line");
        }
    }
}

static void sc_paste_clipboard(sc_tab_t *t)
{
    if (!g_sc.clipboard || g_sc.clipboard_len == 0)
        return;

    int s_cy, s_cx, e_cy, e_cx;
    if (sc_get_selection(t, &s_cy, &s_cx, &e_cy, &e_cx)) {
        sc_delete_range(t, s_cy, s_cx, e_cy, e_cx);
        t->sel_active = 0;
    }

    for (size_t i = 0; i < g_sc.clipboard_len; i++) {
        char c = g_sc.clipboard[i];
        if (c == '\r') continue;
        if (c == '\n')
            sc_insert_newline(t);
        else
            sc_insert_text_char(t, c);
    }
    snprintf(t->status, sizeof(t->status), "Pasted %zu bytes", g_sc.clipboard_len);
    t->dirty = 1;
}

static void sc_duplicate_line(sc_tab_t *t)
{
    sc_line_t *cur = &t->lines[t->cy];
    sc_tab_insert_line(t, t->cy + 1, cur->chars, cur->len);
    t->cy++;
    t->dirty = 1;
    snprintf(t->status, sizeof(t->status), "Line duplicated");
}

static void sc_word_skip_left(sc_tab_t *t)
{
    sc_line_t *l = &t->lines[t->cy];
    if (t->cx == 0) {
        if (t->cy > 0) {
            t->cy--;
            t->cx = t->lines[t->cy].len;
        }
        return;
    }
    while (t->cx > 0 && !isalnum((unsigned char)l->chars[t->cx - 1]) && l->chars[t->cx - 1] != '_')
        t->cx--;
    while (t->cx > 0 && (isalnum((unsigned char)l->chars[t->cx - 1]) || l->chars[t->cx - 1] == '_'))
        t->cx--;
}

static void sc_word_skip_right(sc_tab_t *t)
{
    sc_line_t *l = &t->lines[t->cy];
    if (t->cx >= l->len) {
        if (t->cy + 1 < t->nlines) {
            t->cy++;
            t->cx = 0;
        }
        return;
    }
    while (t->cx < l->len && (isalnum((unsigned char)l->chars[t->cx]) || l->chars[t->cx] == '_'))
        t->cx++;
    while (t->cx < l->len && !isalnum((unsigned char)l->chars[t->cx]) && l->chars[t->cx] != '_')
        t->cx++;
}

static int is_statement_kw(const char *name)
{
    static const char *const kw_spaces[] = {
        "function", "local", "return", "while", "struct", "switch", "case", "default", NULL
    };
    for (int i = 0; kw_spaces[i]; i++) {
        if (strcmp(name, kw_spaces[i]) == 0) return 1;
    }
    return 0;
}

static int sc_try_tab_complete(sc_tab_t *t)
{
    char suf[64];
    char full[64];
    if (!sc_get_autocomplete_suggestion(t, suf, sizeof(suf), full, sizeof(full)))
        return 0;

    sc_line_t *cur = &t->lines[t->cy];
    char to_insert[128];
    snprintf(to_insert, sizeof(to_insert), "%s%s", suf, is_statement_kw(full) ? " " : "");
    int ins_len = (int)strlen(to_insert);
    for (int i = 0; i < ins_len; i++) {
        sc_line_insert_char(cur, t->cx + i, to_insert[i]);
    }
    t->cx += ins_len;
    t->dirty = 1;
    return 1;
}

static void sc_execute_menu_action(void)
{
    sc_tab_t *t = &g_sc.tabs[g_sc.cur_tab];
    g_sc.show_menu = 0;

    switch (g_sc.menu_sel) {
    case MENU_RESUME:
        break;
    case MENU_OPEN: {
        char dir[128];
        snprintf(dir, sizeof(dir), "%s", t->path);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = '\0';
        } else if (slash == dir) {
            dir[1] = '\0';
        } else {
            snprintf(dir, sizeof(dir), "/code-b");
        }
        sc_picker_open(&g_picker, dir);
        break;
    }
    case MENU_SAVE:
        sc_tab_save(t, NULL);
        break;
    case MENU_SAVE_AS:
        /* Prompt or save */
        sc_tab_save(t, NULL);
        break;
    case MENU_CHECK:
        sc_check_syntax(t);
        break;
    case MENU_RUN:
        sc_run_active(t, NULL);
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
    case MENU_NEW_LUA:
        if (g_sc.ntabs < SC_MAX_TABS) {
            int new_idx = g_sc.ntabs++;
            char new_path[64];
            snprintf(new_path, sizeof(new_path), "/code-lua/file%d.lua", new_idx + 1);
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

    /* When file picker is active */
    if (g_picker.active) {
        sc_picker_handle_key(&g_picker, c);
        return;
    }

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
    if (c == 0x03) {                    /* Ctrl+C: Copy */
        sc_copy_selection_or_line(t, 0);
        return;
    }
    if (c == 0x18) {                    /* Ctrl+X: Cut */
        sc_copy_selection_or_line(t, 1);
        sc_scroll_to_cursor(t);
        return;
    }
    if (c == 0x16) {                    /* Ctrl+V: Paste */
        sc_paste_clipboard(t);
        sc_scroll_to_cursor(t);
        return;
    }
    if (c == 0x19) {                    /* Ctrl+Y: Duplicate line */
        sc_duplicate_line(t);
        sc_scroll_to_cursor(t);
        return;
    }
    if (c == 0x0F) {                    /* Ctrl+O: Open file picker */
        char dir[128];
        snprintf(dir, sizeof(dir), "%s", t->path);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) *slash = '\0';
        else if (slash == dir) dir[1] = '\0';
        else snprintf(dir, sizeof(dir), "/code-b");
        sc_picker_open(&g_picker, dir);
        return;
    }
    if (c == 0x13) {                    /* Ctrl+S: Save */
        sc_tab_save(t, NULL);
        return;
    }
    if (c == 0x11) {                    /* Ctrl+Q: Quit */
        g_sc.quit_requested = 1;
        return;
    }
    if (c == 0x12 || c == SC_KEY_F5) {  /* Ctrl+R or F5: Run */
        sc_run_active(t, NULL);
        return;
    }
    if (c == HID_KEY_F6) {              /* F6: Check syntax */
        sc_check_syntax(t);
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

    /* Modifiers state */
    int shift = sc_shift_held();
    int ctrl = sc_ctrl_held();
    int alt = sc_alt_held();

    /* Direct text editing */
    if (c == '\r' || c == '\n') {
        t->sel_active = 0;
        sc_insert_newline(t);
    } else if (c == 0x08 || c == 0x7F) {
        sc_backspace(t);
    } else if (c == HID_KEY_DEL) {
        sc_delete_under_cursor(t);
    } else if (c == '\t') {
        t->sel_active = 0;
        if (!sc_try_tab_complete(t)) {
            sc_insert_text_char(t, ' ');
            sc_insert_text_char(t, ' ');
        }
    } else if (c == HID_KEY_LEFT) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }

        if (ctrl && alt) {
            t->cx = 0;
        } else if (ctrl) {
            sc_word_skip_left(t);
        } else {
            if (t->cx > 0) t->cx--;
        }
    } else if (c == HID_KEY_RIGHT) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }

        if (ctrl && alt) {
            t->cx = t->lines[t->cy].len;
        } else if (ctrl) {
            sc_word_skip_right(t);
        } else {
            if (t->cx < t->lines[t->cy].len) t->cx++;
        }
    } else if (c == HID_KEY_UP) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }

        if (ctrl && alt) {
            t->cy = 0;
            t->cx = 0;
        } else if (alt) {
            t->cy -= (g_sc.screen_rows - 2);
            if (t->cy < 0) t->cy = 0;
        } else {
            if (t->cy > 0) t->cy--;
            if (t->cx > t->lines[t->cy].len) t->cx = t->lines[t->cy].len;
        }
    } else if (c == HID_KEY_DOWN) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }

        if (ctrl && alt) {
            t->cy = t->nlines - 1;
            t->cx = t->lines[t->cy].len;
        } else if (alt) {
            t->cy += (g_sc.screen_rows - 2);
            if (t->cy >= t->nlines) t->cy = t->nlines - 1;
        } else {
            if (t->cy + 1 < t->nlines) t->cy++;
            if (t->cx > t->lines[t->cy].len) t->cx = t->lines[t->cy].len;
        }
    } else if (c == HID_KEY_HOME) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }
        t->cx = 0;
    } else if (c == HID_KEY_END) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }
        t->cx = t->lines[t->cy].len;
    } else if (c == HID_KEY_PGUP) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }
        t->cy -= (g_sc.screen_rows - 2);
        if (t->cy < 0) t->cy = 0;
    } else if (c == HID_KEY_PGDN) {
        if (shift && !t->sel_active) {
            t->sel_active = 1;
            t->sel_anchor_cx = t->cx;
            t->sel_anchor_cy = t->cy;
        } else if (!shift) {
            t->sel_active = 0;
        }
        t->cy += (g_sc.screen_rows - 2);
        if (t->cy >= t->nlines) t->cy = t->nlines - 1;
    } else if (c >= 32 && c < 127) {
        /* Auto-closing brackets and quotes, and overtyping */
        if (c == ')' || c == ']' || c == '}' || c == '"') {
            sc_line_t *cur = &t->lines[t->cy];
            if (t->cx < cur->len && cur->chars[t->cx] == c) {
                t->cx++;
                sc_scroll_to_cursor(t);
                return;
            }
        }
        if (t->sel_active) {
            int s_cy, s_cx, e_cy, e_cx;
            if (sc_get_selection(t, &s_cy, &s_cx, &e_cy, &e_cx)) {
                sc_delete_range(t, s_cy, s_cx, e_cy, e_cx);
                t->sel_active = 0;
            }
        }
        if (c == '(') {
            sc_insert_text_char(t, '(');
            sc_insert_text_char(t, ')');
            t->cx--;
        } else if (c == '[') {
            sc_insert_text_char(t, '[');
            sc_insert_text_char(t, ']');
            t->cx--;
        } else if (c == '{') {
            sc_insert_text_char(t, '{');
            sc_insert_text_char(t, '}');
            t->cx--;
        } else if (c == '"') {
            sc_insert_text_char(t, '"');
            sc_insert_text_char(t, '"');
            t->cx--;
        } else {
            sc_insert_text_char(t, (char)c);
        }
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
        if (g_picker.active)
            sc_draw_picker(&g_picker);

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
    bmnative_disasm_word(0xE2810002u, dis_buf, sizeof(dis_buf)); /* add r0, r1, #2 */
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

    /* Move down in menu and select Apri file */
    sc_handle_key(HID_KEY_DOWN);
    if (g_sc.menu_sel != 1) {
        printf("FAIL: menu navigation down mismatch (%d != 1)\n", g_sc.menu_sel);
        return 15;
    }
    sc_handle_key('\n'); /* Execute Apri file -> opens picker */
    if (g_sc.show_menu || !g_picker.active) {
        printf("FAIL: Apri file did not open picker\n");
        return 16;
    }

    /* In picker: entry 0 is "..", entry 1 is "prova.b". Move down to select "prova.b" */
    sc_handle_key(HID_KEY_DOWN);
    if (g_picker.sel != 1) {
        printf("FAIL: picker navigation down mismatch (%d != 1)\n", g_picker.sel);
        return 17;
    }
    sc_handle_key('\n'); /* Open "prova.b" */
    if (g_picker.active) {
        printf("FAIL: picker did not close after selecting file\n");
        return 18;
    }

    /* Draw test */
    sc_draw_tabs_status();
    sc_draw_editor();
    sc_draw_popup_menu();
    sc_draw_picker(&g_picker);

    /* Test 11: Auto-pairing, Tab completion, Duplicate, Copy/Paste, and Navigations */
    sc_tab_t edit_tab;
    sc_tab_init_empty(&edit_tab, "/code-b/edit_test.b");
    g_sc.tabs[0] = edit_tab;
    g_sc.cur_tab = 0;

    /* Auto-pair bracket */
    sc_handle_key('(');
    if (g_sc.tabs[0].lines[0].len != 2 || g_sc.tabs[0].cx != 1) {
        printf("FAIL: auto-pair bracket\n");
        return 19;
    }
    /* Overtype ')' */
    sc_handle_key(')');
    if (g_sc.tabs[0].lines[0].len != 2 || g_sc.tabs[0].cx != 2) {
        printf("FAIL: overtype bracket\n");
        return 20;
    }

    /* Tab completion: type "func" then '\t' */
    sc_tab_set_text(&g_sc.tabs[0], "func", 4);
    g_sc.tabs[0].cx = 4;
    sc_handle_key('\t');
    if (strcmp(g_sc.tabs[0].lines[0].chars, "function ") != 0) {
        printf("FAIL: tab completion (%s != function )\n", g_sc.tabs[0].lines[0].chars);
        return 21;
    }

    /* Duplicate line (Ctrl+Y = 0x19) */
    sc_handle_key(0x19);
    if (g_sc.tabs[0].nlines != 2 || g_sc.tabs[0].cy != 1) {
        printf("FAIL: duplicate line\n");
        return 22;
    }

    /* Copy (Ctrl+C = 0x03) and Paste (Ctrl+V = 0x16) */
    sc_handle_key(0x03); /* Copy whole line */
    if (!g_sc.clipboard || g_sc.clipboard_len == 0) {
        printf("FAIL: clipboard empty on copy\n");
        return 23;
    }
    sc_handle_key(0x16); /* Paste */
    if (g_sc.tabs[0].nlines < 3) {
        printf("FAIL: paste line\n");
        return 24;
    }

    /* Navigation: Shift+Down selection */
    g_mock_shift = 1;
    sc_handle_key(HID_KEY_DOWN);
    g_mock_shift = 0;
    if (!g_sc.tabs[0].sel_active) {
        printf("FAIL: Shift+Down selection active\n");
        return 25;
    }

    /* Ctrl+Left / Right word skip */
    g_sc.tabs[0].cy = 0;
    g_sc.tabs[0].cx = 0;
    g_mock_ctrl = 1;
    sc_handle_key(HID_KEY_RIGHT);
    g_mock_ctrl = 0;
    if (g_sc.tabs[0].cx <= 0) {
        printf("FAIL: Ctrl+Right word skip (%d <= 0, line: '%s')\n", g_sc.tabs[0].cx, g_sc.tabs[0].lines[0].chars);
        return 26;
    }

    /* Ctrl+Alt+Left: Start of line */
    g_mock_ctrl = 1; g_mock_alt = 1;
    sc_handle_key(HID_KEY_LEFT);
    g_mock_ctrl = 0; g_mock_alt = 0;
    if (g_sc.tabs[0].cx != 0) {
        printf("FAIL: Ctrl+Alt+Left start of line\n");
        return 27;
    }

    /* Ctrl+Alt+Up: Start of file */
    g_mock_ctrl = 1; g_mock_alt = 1;
    sc_handle_key(HID_KEY_UP);
    g_mock_ctrl = 0; g_mock_alt = 0;
    if (g_sc.tabs[0].cy != 0 || g_sc.tabs[0].cx != 0) {
        printf("FAIL: Ctrl+Alt+Up start of file\n");
        return 28;
    }

    /* Alt+Down: Page Down */
    g_mock_alt = 1;
    sc_handle_key(HID_KEY_DOWN);
    g_mock_alt = 0;

    /* Test 12: Autocomplete, doc functions, and 'fn' / 'int' native execution */
    /* Check 'fu' prefix completion */
    sc_tab_set_text(&tab, "fu", 2);
    tab.cy = 0;
    tab.cx = 2;
    if (!sc_try_tab_complete(&tab) || strcmp(tab.lines[0].chars, "function ") != 0) {
        printf("FAIL: short prefix 'fu' tab completion (%s != function )\n", tab.lines[0].chars);
        return 29;
    }

    /* Check doc function autocomplete: define 'fn draw_edges', then 'draw_' -> tab -> 'draw_edges' */
    const char *doc_with_fn = "fn draw_edges(x: int): int\n  return x\nend\ndraw_";
    sc_tab_set_text(&tab, doc_with_fn, strlen(doc_with_fn));
    tab.cy = 3;
    tab.cx = 5; /* end of "draw_" */
    if (!sc_try_tab_complete(&tab) || strcmp(tab.lines[3].chars, "draw_edges") != 0) {
        printf("FAIL: doc function tab completion (%s != draw_edges)\n", tab.lines[3].chars);
        return 30;
    }

    /* Compile and run 'fn' and 'int' types natively */
    const char *fn_int_src =
        "fn calc(a: int): int\n"
        "  local b: int = a * 2\n"
        "  return b + 1\n"
        "end\n";
    sc_tab_set_text(&tab, fn_int_src, strlen(fn_int_src));
    blob = NULL;
    blob_len = 0;
    rc = sc_compile_b(&tab, &blob, &blob_len);
    if (rc != 0 || !blob) {
        printf("FAIL: compile 'fn' and 'int' failed\n");
        return 29;
    }
    m = NULL;
    if (bmnative_load(&m, blob, blob_len, &why) != BMN_OK) {
        printf("FAIL: load 'fn'/'int' module: %s\n", why);
        free(blob);
        return 30;
    }
    fn_idx = bmnative_find(m, "calc");
    if (fn_idx < 0) {
        printf("FAIL: bmnative_find 'calc'\n");
        bmnative_free(m);
        free(blob);
        return 31;
    }
    args[0] = 20;
    ret = 0;
    crc = bmnative_call(m, fn_idx, args, 1, &ret);
    if (crc != BMN_OK || ret != 41) {
        printf("FAIL: calc(20) returned %ld (expected 41)\n", (long)ret);
        bmnative_free(m);
        free(blob);
        return 32;
    }
    bmnative_free(m);
    free(blob);

    /* Test 13: .lua path normalization and syntax detection */
    sc_normalize_path(norm, sizeof(norm), "demo.lua");
    if (strcmp(norm, "/code-lua/demo.lua") != 0) {
        printf("FAIL: lua path norm (%s != /code-lua/demo.lua)\n", norm);
        return 33;
    }
    sc_tab_t lua_tab;
    sc_tab_init_empty(&lua_tab, "demo.lua");
    if (lua_tab.syntax != SC_SYN_LUA) {
        printf("FAIL: lua syntax detection\n");
        sc_tab_free(&lua_tab);
        return 34;
    }

    /* Test 14: Lua syntax check (valid + error line jump) */
    const char *good_lua =
        "local function sq(x)\n"
        "  return x * x\n"
        "end\n"
        "cls(0x101828)\n"
        "return sq(7)\n";
    sc_tab_set_text(&lua_tab, good_lua, strlen(good_lua));
    if (sc_compile_lua(&lua_tab) != 0 || lua_tab.err_line != 0) {
        printf("FAIL: sc_compile_lua valid (%s)\n", lua_tab.status);
        sc_tab_free(&lua_tab);
        return 35;
    }
    if (sc_run_active(&lua_tab, NULL) != 0 || strstr(lua_tab.status, "49") == NULL) {
        printf("FAIL: sc_run_lua return value (%s)\n", lua_tab.status);
        sc_tab_free(&lua_tab);
        return 36;
    }

    const char *bad_lua =
        "local x = 10\n"
        "local y = (\n"
        "return x\n";
    sc_tab_set_text(&lua_tab, bad_lua, strlen(bad_lua));
    if (sc_compile_lua(&lua_tab) == 0 || lua_tab.err_line < 2) {
        printf("FAIL: sc_compile_lua error line (%d)\n", lua_tab.err_line);
        sc_tab_free(&lua_tab);
        return 37;
    }

    /* Test 15: Autocomplete differs cleanly between .lua and .b */
    sc_tab_set_text(&lua_tab, "rectf", 5);
    lua_tab.cy = 0; lua_tab.cx = 5;
    if (!sc_try_tab_complete(&lua_tab) || strcmp(lua_tab.lines[0].chars, "rectfill") != 0) {
        printf("FAIL: lua autocomplete rectfill (%s)\n", lua_tab.lines[0].chars);
        sc_tab_free(&lua_tab);
        return 38;
    }
    sc_tab_set_text(&lua_tab, "i3", 2);
    lua_tab.cy = 0; lua_tab.cx = 2;
    if (sc_try_tab_complete(&lua_tab)) {
        printf("FAIL: lua tab should not autocomplete .b type i32 (%s)\n", lua_tab.lines[0].chars);
        sc_tab_free(&lua_tab);
        return 39;
    }
    sc_tab_free(&lua_tab);

    free(g_sc.clipboard);
    g_sc.clipboard = NULL;
    sc_tab_free(&g_sc.tabs[0]);
    sc_tab_free(&tab);
    printf("shell_code tests passed: 15/15 checks ok\n");
    return 0;
}
