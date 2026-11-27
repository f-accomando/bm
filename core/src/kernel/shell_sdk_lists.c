/*
 * Shell SDK: the named lists of a block of the source (see shell_sdk_lists.h). Original code.
 */
#include "shell_sdk_lists.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_sdk_edit.h"

int sdk_list_up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

int sdk_list_same(const char *a, const char *b)          /* with no regard to case */
{
    while (*a && sdk_list_up((unsigned char)*a) == sdk_list_up((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

int sdk_list_word_char(int c)
{
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* ---- finding the block ---- */

size_t sdk_list_next_line(const char *s, size_t len, size_t p)
{
    while (p < len && s[p] != '\n')
        p++;
    return p < len ? p + 1 : len;
}

/* The first line from p that starts (after spaces) with the marker, followed by no letter or '-'. len: none. */
size_t sdk_list_find_marker(const char *s, size_t len, size_t p, const char *marker)
{
    const size_t n = strlen(marker);
    for (; p < len; p = sdk_list_next_line(s, len, p)) {
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

int sdk_list_line_number(const char *s, size_t p)
{
    int n = 1;
    for (size_t i = 0; i < p; i++)
        n += s[i] == '\n';
    return n;
}

int sdk_list_find(const char *src, size_t len, int c, const char *tag, size_t *from, size_t *to)
{
    char marker[24], end_marker[8];
    snprintf(marker, sizeof marker, "%s@%s", c ? "//" : ";", tag);
    snprintf(end_marker, sizeof end_marker, "%s@end", c ? "//" : ";");
    *from = sdk_list_find_marker(src, len, 0, marker);
    *to = *from;
    if (*from == len)
        return 0;
    const size_t end = sdk_list_find_marker(src, len, sdk_list_next_line(src, len, *from), end_marker);
    if (end == len)
        return -1;
    *to = sdk_list_next_line(src, len, end);
    return 1;
}

/* ---- reading: the lists of values ---- */



int sdk_list_wrong(sdk_reader_t *r, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

int sdk_list_wrong(sdk_reader_t *r, int line, const char *fmt, ...)
{
    char msg[128];
    va_list a;
    va_start(a, fmt);
    vsnprintf(msg, sizeof msg, fmt, a);
    va_end(a);
    snprintf(r->err, r->en, "line %d: %s", line, msg);
    return -1;
}

/* Spaces, comments and (bm C) ends of line. In assembly the end of a line matters: it stays. */
static void blank(sdk_reader_t *r)
{
    for (;;) {
        if (r->p >= r->end)
            return;
        const char c = *r->p;
        if (c == ' ' || c == '\t' || c == '\r' || (c == '\n' && r->c)) {
            r->line += c == '\n';
            r->p++;
        } else if ((!r->c && c == ';') || (r->c && c == '/' && r->p + 1 < r->end && r->p[1] == '/')) {
            while (r->p < r->end && *r->p != '\n')
                r->p++;
        } else if (r->c && c == '/' && r->p + 1 < r->end && r->p[1] == '*') {
            r->p += 2;
            while (r->p < r->end && !(*r->p == '*' && r->p + 1 < r->end && r->p[1] == '/'))
                r->line += *r->p++ == '\n';
            r->p = r->p + 2 < r->end ? r->p + 2 : r->end;
        } else
            return;
    }
}

static int mark(sdk_reader_t *r, char c)
{
    blank(r);
    if (r->p < r->end && *r->p == c) {
        r->p++;
        return 1;
    }
    return 0;
}

static int read_word(sdk_reader_t *r, char *out, size_t max)
{
    blank(r);
    size_t n = 0;
    if (r->p >= r->end || !sdk_list_word_char((unsigned char)*r->p) || (*r->p >= '0' && *r->p <= '9'))
        return 0;
    while (r->p < r->end && sdk_list_word_char((unsigned char)*r->p)) {
        if (n + 1 < max)
            out[n++] = *r->p;
        r->p++;
    }
    out[n] = 0;
    return 1;
}

static int read_number(sdk_reader_t *r, int *out)
{
    blank(r);
    const char *s = r->p;
    int base = 10;
    if (s < r->end && *s == '$') {
        base = 16;
        s++;
    } else if (s < r->end && *s == '%') {
        base = 2;
        s++;
    } else if (r->end - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    long v = 0;
    int digits = 0;
    for (; s < r->end; s++, digits++) {
        const int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                      *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99;
        if (d >= base)
            break;
        v = v * base + d;
        if (v > 0xFFFF)
            return 0;
    }
    if (!digits)
        return 0;
    r->p = s;
    *out = (int)v;
    return 1;
}

/* One value: words and numbers joined by +, one word at most. Into the last item. */
static int read_value(sdk_reader_t *r)
{
    sdk_value_t v = { "", 0, 0 };
    blank(r);
    v.line = r->line;
    do {
        char w[24];
        int n;
        if (read_word(r, w, sizeof w)) {
            if (v.word[0])
                return sdk_list_wrong(r, v.line, "'%s+%s': one name in a value", v.word, w);
            if (strlen(w) >= SDK_LIST_NAME)
                return sdk_list_wrong(r, v.line, "'%s' is too long: %d letters at most", w, SDK_LIST_NAME - 1);
            snprintf(v.word, sizeof v.word, "%.*s", SDK_LIST_NAME - 1, w);
        } else if (read_number(r, &n))
            v.num += n;
        else
            return sdk_list_wrong(r, v.line, "a value is expected here, a number or a name");
    } while (mark(r, '+'));
    if (r->values == r->values_max) {
        const int max = r->values_max ? r->values_max * 2 : 512;
        sdk_value_t *p = max > 20000 ? NULL : realloc(r->value, (size_t)max * sizeof *p);
        if (!p)
            return sdk_list_wrong(r, v.line, "the %s block is too long", r->what);
        r->value = p;
        r->values_max = max;
    }
    r->value[r->values++] = v;
    r->item[r->items - 1].count++;
    return 0;
}

static int new_item(sdk_reader_t *r, const char *name)
{
    if (r->items == (int)(sizeof r->item / sizeof r->item[0]))
        return sdk_list_wrong(r, r->line, "too many lists");
    for (int i = 0; i < r->items; i++)
        if (sdk_list_same(r->item[i].name, name))
            return sdk_list_wrong(r, r->line, "'%s' is already there, at line %d", name, r->item[i].line);
    sdk_item_t *it = &r->item[r->items++];
    snprintf(it->name, sizeof it->name, "%s", name);
    it->line = r->line;
    it->first = r->values;
    it->count = 0;
    return 0;
}

/* label:  then .byte lines */
static int read_asm(sdk_reader_t *r)
{
    while (r->p < r->end) {
        char w[24];
        blank(r);
        if (r->p < r->end && *r->p == '\n') {
            r->p++;
            r->line++;
            continue;
        }
        if (r->p >= r->end)
            break;
        if (read_word(r, w, sizeof w)) {
            if (!mark(r, ':'))
                return sdk_list_wrong(r, r->line, "the %s block has labels and .byte lines only: '%s' needs a ':'", r->what, w);
            if (new_item(r, w) != 0)
                return -1;
            blank(r);
            if (r->p >= r->end || *r->p == '\n')
                continue;
        }
        if (!mark(r, '.') || !read_word(r, w, sizeof w) || (!sdk_list_same(w, "byte") && !sdk_list_same(w, "db")))
            return sdk_list_wrong(r, r->line, "the %s block has labels and .byte lines only", r->what);
        if (!r->items)
            return sdk_list_wrong(r, r->line, "these values need a label before them");
        do {
            if (read_value(r) != 0)
                return -1;
        } while (mark(r, ','));
        blank(r);
        if (r->p < r->end && *r->p != '\n')
            return sdk_list_wrong(r, r->line, "a ',' or the end of the line is expected");
    }
    return 0;
}

/* const byte name[] = { values }; */
static int read_c(sdk_reader_t *r)
{
    for (;;) {
        char w[24];
        blank(r);
        if (r->p >= r->end)
            return 0;
        if (!read_word(r, w, sizeof w) || strcmp(w, "const"))
            return sdk_list_wrong(r, r->line, "the %s block has const byte name[] = { } only", r->what);
        if (read_word(r, w, sizeof w) && !strcmp(w, "unsigned"))
            read_word(r, w, sizeof w);
        if (strcmp(w, "byte") && strcmp(w, "char"))
            return sdk_list_wrong(r, r->line, "the %s block is made of bytes: const byte name[]", r->what);
        if (!read_word(r, w, sizeof w))
            return sdk_list_wrong(r, r->line, "a name is expected after const byte");
        if (new_item(r, w) != 0)
            return -1;
        if (!mark(r, '['))
            return sdk_list_wrong(r, r->line, "'%s' is a list: %s[]", w, w);
        int size;
        read_number(r, &size);
        if (!mark(r, ']') || !mark(r, '=') || !mark(r, '{'))
            return sdk_list_wrong(r, r->line, "expected %s[] = {", w);
        while (!mark(r, '}')) {
            if (read_value(r) != 0)
                return -1;
            if (!mark(r, ',')) {
                if (!mark(r, '}'))
                    return sdk_list_wrong(r, r->line, "a ',' or '}' is expected");
                break;
            }
        }
        mark(r, ';');
    }
}

/* ---- writing ---- */


void sdk_list_add(sdk_text_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void sdk_list_add(sdk_text_t *t, const char *fmt, ...)
{
    char b[256];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (n >= (int)sizeof b)
        n = sizeof b - 1;
    if (t->len + (size_t)n + 1 > t->max) {
        if (!t->grow) {
            t->full = 1;
            return;
        }
        const size_t max = (t->max ? t->max * 2 : 8192) + (size_t)n;
        char *s = realloc(t->s, max);
        if (!s) {
            t->full = 1;
            return;
        }
        t->s = s;
        t->max = max;
    }
    memcpy(t->s + t->len, b, (size_t)n + 1);
    t->len += (size_t)n;
}

void sdk_list_add_str(sdk_text_t *t, const char *s)           /* of any length */
{
    for (size_t n = strlen(s), at = 0; at < n; at += 200)
        sdk_list_add(t, "%.200s", s + at);
}


void sdk_list_list_flush(sdk_list_t *l, int last)
{
    if (!l->has)
        return;
    char v[200];
    snprintf(v, sizeof v, l->c ? "    %s%s" : "    .byte %s", l->line, !l->c ? "" : last ? " };" : ",");
    if (l->note[0])
        sdk_list_add(l->t, "%-44s %s %s\n", v, l->c ? "//" : ";", l->note);
    else
        sdk_list_add(l->t, "%s\n", v);
    l->has = 0;
}

/* One line of values; the one before goes out. */
void sdk_list_list_line(sdk_list_t *l, const char *values, const char *note)
{
    sdk_list_list_flush(l, 0);
    snprintf(l->line, sizeof l->line, "%s", values);
    snprintf(l->note, sizeof l->note, "%s", note);
    l->has = 1;
}

void sdk_list_list_open(sdk_list_t *l, sdk_text_t *t, int c, const char *name, const char *note)
{
    l->t = t;
    l->c = c;
    l->has = 0;
    if (c)
        sdk_list_add(t, note[0] ? "const byte %s[] = {          // %s\n" : "const byte %s[] = {%s\n", name, note);
    else
        sdk_list_add(t, note[0] ? "%s:                          ; %s\n" : "%s:%s\n", name, note);
}

size_t sdk_list_put(char *line, size_t at, size_t max, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

size_t sdk_list_put(char *line, size_t at, size_t max, const char *fmt, ...)
{
    if (at >= max - 1)
        return at;
    va_list a;
    va_start(a, fmt);
    const int n = vsnprintf(line + at, max - at, fmt, a);
    va_end(a);
    return at + (size_t)n < max ? at + (size_t)n : max - 1;
}

int sdk_list_read(const char *src, size_t len, int c, const char *tag, sdk_reader_t **out, char *err, size_t en)
{
    size_t from, to;
    const int found = sdk_list_find(src, len, c, tag, &from, &to);
    *out = NULL;
    if (found < 0) {
        snprintf(err, en, "line %d: %s@%s has no %s@end", sdk_list_line_number(src, from), c ? "//" : ";", tag,
                 c ? "//" : ";");
        return -1;
    }
    if (!found)
        return 0;
    sdk_reader_t *r = calloc(1, sizeof *r);
    if (!r) {
        snprintf(err, en, "line 1: not enough memory for the %s", tag);
        return -1;
    }
    r->p = src + sdk_list_next_line(src, len, from);
    size_t end = to;                            /* back to the start of the line of @end */
    while (end > from && src[end - 1] == '\n')
        end--;
    while (end > from && src[end - 1] != '\n')
        end--;
    r->end = src + end;
    if (r->p > r->end)
        r->p = r->end;
    r->line = sdk_list_line_number(src, (size_t)(r->p - src));
    r->c = c;
    r->what = tag;
    r->err = err;
    r->en = en;
    if ((c ? read_c(r) : read_asm(r)) != 0) {
        sdk_list_free(r);
        return -1;
    }
    *out = r;
    return 1;
}

void sdk_list_free(sdk_reader_t *r)
{
    if (r)
        free(r->value);
    free(r);
}

int sdk_list_commit(const char *tag, int c, const char *text, size_t n, int run, char *err, size_t en)
{
    const char *src = sdk_edit_text();
    const size_t len = sdk_edit_length();
    size_t from, to;
    const int found = sdk_list_find(src, len, c, tag, &from, &to);
    if (found < 0) {
        snprintf(err, en, "line %d: %s@%s has no %s@end", sdk_list_line_number(src, from), c ? "//" : ";", tag,
                 c ? "//" : ";");
        return -1;
    }
    sdk_text_t t = { NULL, 0, 0, 0, 1 };
    if (!found) {                               /* before the music, else before the map, or at the end */
        from = strcmp(tag, "music") ? sdk_list_find_marker(src, len, 0, c ? "//@music" : ";@music") : len;
        if (from == len)
            from = sdk_list_find_marker(src, len, 0, c ? "//@map" : ";@map");
        to = from;
        if (from == len && len && src[len - 1] != '\n')
            sdk_list_add(&t, "\n");
    }
    sdk_list_add_str(&t, text);
    if (!found && to < len)
        sdk_list_add(&t, "\n");
    int rc = 0;
    (void)n;
    if (t.full) {
        snprintf(err, en, "not enough memory");
        rc = -1;
    } else if (sdk_edit_replace(from, to, t.s, t.len, run) != 0) {
        snprintf(err, en, "the code is full: the %s is not written", tag);
        rc = -1;
    }
    free(t.s);
    return rc;
}
