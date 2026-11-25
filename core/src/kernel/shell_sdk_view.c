/*
 * Shell SDK: a cursor in a text that is only read (see shell_sdk_view.h). Original code.
 */
#include "shell_sdk_view.h"

#include <string.h>

static int is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int line_of(const sdk_view_t *v, sdk_view_line_t fn, void *ctx, int line, char *out)
{
    out[0] = 0;
    if (line < 0 || line >= v->lines)
        return 0;
    const int n = fn(ctx, line, out, SDK_VIEW_LINE);
    return n < 0 ? 0 : n > SDK_VIEW_LINE - 1 ? SDK_VIEW_LINE - 1 : n;
}

void sdk_view_reset(sdk_view_t *v, int lines)
{
    memset(v, 0, sizeof *v);
    v->lines = lines;
}

void sdk_view_move(sdk_view_t *v, int move, int select, int page, sdk_view_line_t fn, void *ctx)
{
    if (v->lines < 1)
        return;
    if (select && !v->sel) {
        v->aline = v->line;
        v->acol = v->col;
    }
    v->sel = select != 0;
    if (page < 1)
        page = 1;

    char s[SDK_VIEW_LINE];
    int n = line_of(v, fn, ctx, v->line, s);
    int vertical = 0;
    switch (move) {
    case SDK_VIEW_LEFT:
        if (v->col > 0)
            v->col--;
        else if (v->line > 0)
            v->col = line_of(v, fn, ctx, --v->line, s);
        break;
    case SDK_VIEW_RIGHT:
        if (v->col < n)
            v->col++;
        else if (v->line < v->lines - 1) {
            v->line++;
            v->col = 0;
        }
        break;
    case SDK_VIEW_WORD_LEFT:
        if (v->col == 0) {
            if (v->line > 0)
                v->col = line_of(v, fn, ctx, --v->line, s);
        } else {
            while (v->col > 0 && !is_word((unsigned char)s[v->col - 1]))
                v->col--;
            while (v->col > 0 && is_word((unsigned char)s[v->col - 1]))
                v->col--;
        }
        break;
    case SDK_VIEW_WORD_RIGHT:
        if (v->col >= n) {
            if (v->line < v->lines - 1) {
                v->line++;
                v->col = 0;
            }
        } else {
            while (v->col < n && is_word((unsigned char)s[v->col]))
                v->col++;
            while (v->col < n && !is_word((unsigned char)s[v->col]))
                v->col++;
        }
        break;
    case SDK_VIEW_HOME: v->col = 0; break;
    case SDK_VIEW_END: v->col = n; break;
    case SDK_VIEW_UP: v->line--; vertical = 1; break;
    case SDK_VIEW_DOWN: v->line++; vertical = 1; break;
    case SDK_VIEW_PGUP: v->line -= page; v->top -= page; vertical = 1; break;
    case SDK_VIEW_PGDN: v->line += page; v->top += page; vertical = 1; break;
    case SDK_VIEW_DOC_START: v->line = v->col = 0; break;
    case SDK_VIEW_DOC_END:
        v->line = v->lines - 1;
        v->col = line_of(v, fn, ctx, v->line, s);
        break;
    default: break;
    }
    if (v->line > v->lines - 1)
        v->line = v->lines - 1;
    if (v->line < 0)
        v->line = 0;
    if (v->top < 0)
        v->top = 0;
    if (vertical) {
        n = line_of(v, fn, ctx, v->line, s);
        v->col = v->want < n ? v->want : n;
    } else
        v->want = v->col;
}

void sdk_view_select_all(sdk_view_t *v, sdk_view_line_t fn, void *ctx)
{
    if (v->lines < 1)
        return;
    char s[SDK_VIEW_LINE];
    v->sel = 1;
    v->aline = v->acol = 0;
    v->line = v->lines - 1;
    v->col = v->want = line_of(v, fn, ctx, v->line, s);
}

int sdk_view_selection(const sdk_view_t *v, int *l1, int *c1, int *l2, int *c2)
{
    if (!v->sel || (v->aline == v->line && v->acol == v->col))
        return 0;
    const int back = v->line < v->aline || (v->line == v->aline && v->col < v->acol);
    *l1 = back ? v->line : v->aline;
    *c1 = back ? v->col : v->acol;
    *l2 = back ? v->aline : v->line;
    *c2 = back ? v->acol : v->col;
    return 1;
}

size_t sdk_view_copy(const sdk_view_t *v, sdk_view_line_t fn, void *ctx, char *out, size_t max, int *cut)
{
    int l1, c1, l2, c2;
    if (!sdk_view_selection(v, &l1, &c1, &l2, &c2)) {       /* the line, with its end */
        l1 = v->line;
        l2 = v->line + 1;
        c1 = c2 = 0;
    }
    size_t used = 0;
    *cut = 0;
    for (int l = l1; l <= l2 && l < v->lines + 1; l++) {
        char s[SDK_VIEW_LINE];
        const int n = line_of(v, fn, ctx, l, s);
        const int from = l == l1 ? (c1 < n ? c1 : n) : 0;
        const int to = l == l2 ? (c2 < n ? c2 : n) : n;
        for (int i = from; i < to + (l < l2); i++) {        /* a line before the last has its '\n' */
            if (used >= max) {
                *cut = 1;
                return used;
            }
            out[used++] = i < to ? s[i] : '\n';
        }
    }
    return used;
}

void sdk_view_scroll(sdk_view_t *v, int h, int w)
{
    if (h < 1)
        h = 1;
    if (w < 1)
        w = 1;
    if (v->top > v->lines - h)
        v->top = v->lines - h;
    if (v->top > v->line)
        v->top = v->line;
    if (v->line >= v->top + h)
        v->top = v->line - h + 1;
    if (v->top < 0)
        v->top = 0;
    if (v->col < v->left)
        v->left = v->col;
    if (v->col >= v->left + w)
        v->left = v->col - w + 1;
}
