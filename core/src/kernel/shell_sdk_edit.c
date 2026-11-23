/*
 * Shell SDK: the text buffer of the Code tab (see shell_sdk_edit.h). Original code.
 */
#include "shell_sdk_edit.h"

#include <string.h>

static char buf[SDK_EDIT_MAX];
static size_t len;
static size_t cur;                      /* the cursor: an offset in buf */
static int want;                        /* the column that Up and Down try to keep */

void sdk_edit_reset(void)
{
    len = cur = 0;
    want = 0;
}

size_t sdk_edit_length(void) { return len; }
const char *sdk_edit_text(void) { return buf; }

static size_t line_start(size_t p)
{
    while (p > 0 && buf[p - 1] != '\n')
        p--;
    return p;
}

static size_t line_end(size_t p)
{
    while (p < len && buf[p] != '\n')
        p++;
    return p;
}

static int insert(char c)
{
    if (len >= SDK_EDIT_MAX)
        return 0;
    memmove(buf + cur + 1, buf + cur, len - cur);
    buf[cur++] = c;
    len++;
    return 1;
}

static void keep_column(void) { want = (int)(cur - line_start(cur)); }

int sdk_edit_type(int c)
{
    int changed = 0;
    if (c == '\r' || c == '\n')
        changed = insert('\n');
    else if (c == '\t') {
        int col = (int)(cur - line_start(cur));
        do
            changed |= insert(' ');
        while (++col % 4);
    } else if (c >= 32 && c <= 126)
        changed = insert((char)c);
    if (changed)
        keep_column();
    return changed;
}

int sdk_edit_backspace(void)
{
    if (cur == 0)
        return 0;
    memmove(buf + cur - 1, buf + cur, len - cur);
    cur--;
    len--;
    keep_column();
    return 1;
}

int sdk_edit_delete(void)
{
    if (cur >= len)
        return 0;
    memmove(buf + cur, buf + cur + 1, len - cur - 1);
    len--;
    return 1;
}

void sdk_edit_left(void)
{
    if (cur > 0)
        cur--;
    keep_column();
}

void sdk_edit_right(void)
{
    if (cur < len)
        cur++;
    keep_column();
}

void sdk_edit_home(void)
{
    cur = line_start(cur);
    keep_column();
}

void sdk_edit_end(void)
{
    cur = line_end(cur);
    keep_column();
}

void sdk_edit_up(void)
{
    const size_t s = line_start(cur);
    if (s == 0)
        return;
    const size_t ps = line_start(s - 1);
    const size_t plen = s - 1 - ps;
    cur = ps + ((size_t)want < plen ? (size_t)want : plen);
}

void sdk_edit_down(void)
{
    const size_t e = line_end(cur);
    if (e >= len)
        return;
    const size_t ns = e + 1;
    const size_t nlen = line_end(ns) - ns;
    cur = ns + ((size_t)want < nlen ? (size_t)want : nlen);
}

void sdk_edit_pos(int *line, int *col)
{
    int l = 0;
    for (size_t i = 0; i < cur; i++)
        if (buf[i] == '\n')
            l++;
    *line = l;
    *col = (int)(cur - line_start(cur));
}

int sdk_edit_line_count(void)
{
    int l = 1;
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n')
            l++;
    return l;
}

int sdk_edit_line(int line, const char **s)
{
    size_t p = 0;
    for (int l = 0; l < line; l++) {
        p = line_end(p);
        if (p >= len) {
            *s = NULL;
            return 0;
        }
        p++;
    }
    *s = buf + p;
    return (int)(line_end(p) - p);
}
