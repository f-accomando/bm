/*
 * Shell SDK: the sheet as assembly (see shell_sdk_chr.h). Original code.
 */
#include "shell_sdk_chr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_sdk_sheet.h"
#include "shell_sdk_edit.h"
#include "shell_sdk_tiles.h"

#define TILES (SDK_SHEET_W * SDK_SHEET_H)

int sdk_chr_format(int nes) { return nes ? SDK_TILE_NES : SDK_TILE_SNES4; }

/* ---- writing ---- */

typedef struct { char *out; size_t max, len; int full; } text_t;

static void add(text_t *t, const char *s)
{
    const size_t n = strlen(s);
    if (t->len + n + 1 > t->max) {
        t->full = 1;
        return;
    }
    memcpy(t->out + t->len, s, n + 1);
    t->len += n;
}

long sdk_chr_text(int nes, char *out, size_t max)
{
    const int fmt = sdk_chr_format(nes);
    const int bytes = sdk_tile_bytes(fmt);
    int last = -1;
    for (int i = 0; i < TILES; i++)
        if (sdk_sheet_tile(i / SDK_SHEET_W, i % SDK_SHEET_W)->used)
            last = i;
    if (max)
        out[0] = 0;
    if (last < 0)
        return 0;

    text_t t = { out, max, 0, 0 };
    const int c = nes == SDK_CHR_NES_C;
    add(&t, c ? "//@tiles  from the Sprite tab: 2bpp, 16 bytes a tile, the CHR ROM\ntiles {\n"
              : nes ? ";@tiles  from the Sprite tab: 2bpp, 16 bytes a tile, the CHR ROM\n.chr\n"
                    : ";@tiles  from the Sprite tab: 4bpp, 32 bytes a tile\nsprites_chr:\n");
    for (int i = 0; i <= last; i++) {
        const sdk_tile_t *tile = sdk_sheet_tile(i / SDK_SHEET_W, i % SDK_SHEET_W);
        char line[128];
        if (!tile->used) {                              /* a run of empty tiles is one line */
            int n = 1;
            while (i + n <= last && !sdk_sheet_tile((i + n) / SDK_SHEET_W, (i + n) % SDK_SHEET_W)->used)
                n++;
            if (!c)
                snprintf(line, sizeof line, "    .res %d,0   ; $%03X-$%03X\n", n * bytes, i, i + n - 1);
            else if (n == 1)
                snprintf(line, sizeof line, "    blank(1), // $%03X\n", i);
            else
                snprintf(line, sizeof line, "    blank(%d), // $%03X-$%03X\n", n, i, i + n - 1);
            add(&t, line);
            i += n - 1;
            continue;
        }
        uint8_t data[64];
        sdk_tile_encode(tile->pix, fmt, data);
        if (c) {                                        /* one line a tile, in C numbers */
            static const char hexc[] = "0123456789ABCDEF";
            char row[128];
            size_t k = 4;
            memcpy(row, "    ", k);
            for (int b = 0; b < bytes; b++) {
                row[k++] = '0';
                row[k++] = 'x';
                row[k++] = hexc[data[b] >> 4];
                row[k++] = hexc[data[b] & 15];
                row[k++] = ',';
            }
            snprintf(row + k, sizeof row - k, " // $%03X\n", i);
            add(&t, row);
            continue;
        }
        for (int row = 0; row < bytes; row += 16) {
            static const char hex[] = "0123456789ABCDEF";
            size_t k = 8;                               /* by hand: written again at each painted pixel */
            memcpy(line, "    .db ", k);
            for (int b = 0; b < 16; b++) {
                if (b)
                    line[k++] = ',';
                line[k++] = '$';
                line[k++] = hex[data[row + b] >> 4];
                line[k++] = hex[data[row + b] & 15];
            }
            if (row == 0)
                k += (size_t)snprintf(line + k, sizeof line - k, " ; $%03X", i);
            snprintf(line + k, sizeof line - k, "\n");
            add(&t, line);
        }
    }
    add(&t, c ? "}\n//@tiles-end\n" : nes ? ".endchr\n;@tiles-end\n" : ";@tiles-end\n");
    return t.full ? -1 : (long)t.len;
}

/* ---- finding the block ---- */

/* The start of the line after the one at p. */
static size_t next_line(const char *s, size_t len, size_t p)
{
    while (p < len && s[p] != '\n')
        p++;
    return p < len ? p + 1 : len;
}

/* The first line from p that starts (after spaces) with the marker, followed by no letter or '-'. len: none. */
static size_t find_marker(const char *s, size_t len, size_t p, const char *marker)
{
    const size_t n = strlen(marker);
    for (; p < len; p = next_line(s, len, p)) {
        size_t q = p;
        while (q < len && (s[q] == ' ' || s[q] == '\t'))
            q++;
        if (len - q >= n && !memcmp(s + q, marker, n)) {
            const char c = q + n < len ? s[q + n] : ' ';
            if (c != '-' && !(c >= 'a' && c <= 'z'))
                return p;
        }
    }
    return len;
}

/* The block of a source: [*from, *to), with its last line. Returns 1, 0 when there is none, -1 when it has no
 * end. */
static int find_block(int c, const char *s, size_t len, size_t *from, size_t *to)
{
    *from = find_marker(s, len, 0, c ? "//@tiles" : ";@tiles");
    if (*from == len)
        return 0;
    const size_t end = find_marker(s, len, *from, c ? "//@tiles-end" : ";@tiles-end");
    if (end == len)
        return -1;
    *to = next_line(s, len, end);
    return 1;
}

static int line_number(const char *s, size_t p)
{
    int n = 1;
    for (size_t i = 0; i < p; i++)
        n += s[i] == '\n';
    return n;
}

/* ---- reading ---- */

static int number(const char **p, const char *end, long *out)
{
    const char *s = *p;
    int base = 10;
    if (s < end && *s == '$') {
        base = 16;
        s++;
    } else if (s < end && *s == '%') {
        base = 2;
        s++;
    }
    long v = 0;
    int digits = 0;
    for (; s < end; s++, digits++) {
        const int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                      *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99;
        if (d >= base)
            break;
        v = v * base + d;
        if (v > 0xFFFFFF)
            return -1;
    }
    if (!digits)
        return -1;
    *p = s;
    *out = v;
    return 0;
}

static void spaces(const char **p, const char *end)
{
    while (*p < end && (**p == ' ' || **p == '\t'))
        (*p)++;
}

static int word(const char **p, const char *end, const char *w)
{
    const size_t n = strlen(w);
    if ((size_t)(end - *p) < n || memcmp(*p, w, n) || ((size_t)(end - *p) > n && (*p)[n] != ' ' && (*p)[n] != '\t'))
        return 0;
    *p += n;
    spaces(p, end);
    return 1;
}

/* One line of the block: its bytes go to data, and set[] says which came from .db. Returns 0, or -1 when the
 * line is not understood or the tiles are too many. */
static int read_line(const char *p, const char *end, uint8_t *data, uint8_t *set, size_t *n, size_t max)
{
    const char *semi = memchr(p, ';', (size_t)(end - p));
    if (semi)
        end = semi;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    spaces(&p, end);
    const char *q = p;                                  /* a label before the rest */
    while (q < end && (*q == '_' || *q == '@' || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                       (*q >= '0' && *q <= '9')))
        q++;
    if (q > p && q < end && *q == ':') {
        p = q + 1;
        spaces(&p, end);
    }
    if (p == end || word(&p, end, ".chr") || word(&p, end, ".endchr"))
        return p == end ? 0 : -1;
    long v;
    if (word(&p, end, ".db") || word(&p, end, ".byte")) {
        for (;;) {
            if (number(&p, end, &v) != 0 || v > 255 || *n >= max)
                return -1;
            data[*n] = (uint8_t)v;
            set[(*n)++] = 1;
            spaces(&p, end);
            if (p == end)
                return 0;
            if (*p++ != ',')
                return -1;
            spaces(&p, end);
        }
    }
    if (word(&p, end, ".res") || word(&p, end, ".ds")) {
        long fill = 0;
        if (number(&p, end, &v) != 0)
            return -1;
        spaces(&p, end);
        if (p < end) {
            if (*p++ != ',')
                return -1;
            spaces(&p, end);
            if (number(&p, end, &fill) != 0 || fill > 255)
                return -1;
            spaces(&p, end);
        }
        if (p != end || (size_t)v > max - *n)
            return -1;
        memset(data + *n, (int)fill, (size_t)v);
        memset(set + *n, fill != 0, (size_t)v);
        *n += (size_t)v;
        return 0;
    }
    return -1;
}

/* One line of a tiles { } block of C: numbers and blank(n), between commas. Returns 0, or -1. */
static int read_c_line(const char *p, const char *end, uint8_t *data, uint8_t *set, size_t *n, size_t max)
{
    for (const char *q = p; q + 1 < end; q++)
        if (q[0] == '/' && q[1] == '/')
            end = q;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    spaces(&p, end);
    if (end - p >= 5 && !memcmp(p, "tiles", 5)) {
        p += 5;
        spaces(&p, end);
        if (p == end || *p++ != '{')
            return -1;
    }
    while (spaces(&p, end), p < end) {
        long v;
        if (*p == '}' || *p == ',') {
            p++;
            continue;
        }
        if (end - p > 6 && !memcmp(p, "blank(", 6)) {
            p += 6;
            spaces(&p, end);
            if (number(&p, end, &v) != 0)
                return -1;
            spaces(&p, end);
            if (p == end || *p++ != ')' || v < 0 || (size_t)v * 16 > max - *n)
                return -1;
            memset(data + *n, 0, (size_t)v * 16);
            memset(set + *n, 0, (size_t)v * 16);
            *n += (size_t)v * 16;
            continue;
        }
        if (end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X' || p[1] == 'b' || p[1] == 'B')) {
            const int base = p[1] == 'x' || p[1] == 'X' ? 16 : 2;
            int digits = 0;
            for (p += 2, v = 0; p < end && v < 256; p++, digits++) {
                const int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
                              *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : 99;
                if (d >= base)
                    break;
                v = v * base + d;
            }
            if (!digits)
                return -1;
        } else if (number(&p, end, &v) != 0)
            return -1;
        if (v > 255 || *n >= max)
            return -1;
        data[*n] = (uint8_t)v;
        set[(*n)++] = 1;
    }
    return 0;
}

int sdk_chr_read(int nes, const char *src, size_t len, char *err, size_t en)
{
    const int fmt = sdk_chr_format(nes);
    const size_t bytes = (size_t)sdk_tile_bytes(fmt), max = TILES * bytes;
    size_t from = 0, to = 0;
    const int c = nes == SDK_CHR_NES_C;
    const int found = find_block(c, src, len, &from, &to);
    if (found < 0) {
        snprintf(err, en, "line %d: no %s@tiles-end", line_number(src, from), c ? "//" : ";");
        return -1;
    }
    if (c && !found && find_marker(src, len, 0, "tiles") < len) {
        snprintf(err, en, "line %d: tiles without //@tiles", line_number(src, find_marker(src, len, 0, "tiles")));
        return -1;
    }
    uint8_t *data = calloc(2, max);
    if (!data) {
        snprintf(err, en, "not enough memory");
        return -1;
    }
    uint8_t *set = data + max;
    size_t n = 0;
    if (found)
        for (size_t p = next_line(src, len, from), e; p < to; p = e) {
            e = next_line(src, len, p);
            size_t stop = e;
            while (stop > p && src[stop - 1] == '\n')
                stop--;
            if (find_marker(src + p, stop - p, 0, c ? "//@tiles-end" : ";@tiles-end") < stop - p)
                continue;
            if ((c ? read_c_line : read_line)(src + p, src + stop, data, set, &n, max) != 0) {
                snprintf(err, en, "line %d of the tiles block", line_number(src, p));
                free(data);
                return -1;
            }
        }
    sdk_sheet_clear(0, 0, SDK_SHEET_H - 1, SDK_SHEET_W - 1);
    for (size_t i = 0; i * bytes < n; i++) {
        int used = 0;
        for (size_t k = i * bytes; k < (i + 1) * bytes; k++)
            used |= set[k];
        if (!used)
            continue;
        sdk_tile_t t = { .used = 1, .fmt = (uint8_t)fmt };
        sdk_tile_decode(data, (i + 1) * bytes, i * bytes, fmt, t.pix);
        sdk_sheet_set((int)(i / SDK_SHEET_W), (int)(i % SDK_SHEET_W), &t);
    }
    free(data);
    return 0;
}

/* ---- into the editor ---- */

int sdk_chr_commit(int nes, int run, char *err, size_t en)
{
    const char *src = sdk_edit_text();
    const size_t len = sdk_edit_length();
    size_t from = 0, to = 0;
    const int c = nes == SDK_CHR_NES_C;
    const int found = find_block(c, src, len, &from, &to);
    if (found < 0) {
        snprintf(err, en, "line %d: no %s@tiles-end", line_number(src, from), c ? "//" : ";");
        return -1;
    }
    char *text = malloc(SDK_EDIT_MAX);
    if (!text) {
        snprintf(err, en, "not enough memory");
        return -1;
    }
    const char *before = "", *after = "";
    if (!found) {                               /* before the ;@end of the sprites section, or a new section */
        const size_t section = find_marker(src, len, 0, c ? "//@sprites" : ";@sprites");
        const size_t end = section < len ? find_marker(src, len, section, c ? "//@end" : ";@end") : len;
        if (section < len && end < len)
            from = to = end;
        else {
            from = to = len;
            const int open_line = len && src[len - 1] != '\n';
            before = c ? (open_line ? "\n\n//@sprites\n" : "\n//@sprites\n")
                       : (open_line ? "\n\n;@sprites\n" : "\n;@sprites\n");
            after = c ? "//@end\n" : ";@end\n";
        }
    }
    const size_t head = strlen(before);
    memcpy(text, before, head);
    const long n = sdk_chr_text(nes, text + head, SDK_EDIT_MAX - head - strlen(after));
    int rc = 0;
    if (n < 0) {
        snprintf(err, en, "the tiles do not fit in the code");
        rc = -1;
    } else if (n > 0 || found) {                /* an empty sheet with no block: nothing to write */
        const size_t total = n ? head + (size_t)n + strlen(after) : 0;
        if (n)
            memcpy(text + head + n, after, strlen(after));
        if (sdk_edit_replace(from, to, text, total, run) != 0) {
            snprintf(err, en, "the code is full: tiles not written");
            rc = -1;
        }
    }
    free(text);
    return rc;
}
