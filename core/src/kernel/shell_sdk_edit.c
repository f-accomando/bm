/*
 * Shell SDK: the text buffer of the Code tab (see shell_sdk_edit.h). Original code.
 */
#include "shell_sdk_edit.h"

#include <string.h>

static char buf[SDK_EDIT_MAX];
static size_t len;
static size_t cur;                      /* the cursor: an offset in buf */
static int want;                        /* the column that Up and Down try to keep */
static long anchor = -1;                /* the other end of the selection (-1: none) */
static char clip[SDK_EDIT_MAX + 1];
static size_t clip_len;

void sdk_edit_reset(void)
{
    len = cur = 0;
    want = 0;
    anchor = -1;
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

/* ---- selection ---- */

int sdk_edit_selection(size_t *from, size_t *to)
{
    if (anchor < 0 || (size_t)anchor == cur)
        return 0;
    *from = (size_t)anchor < cur ? (size_t)anchor : cur;
    *to = (size_t)anchor < cur ? cur : (size_t)anchor;
    return 1;
}

/* Removes [from, to) and puts the cursor at from. */
static void remove_range(size_t from, size_t to)
{
    memmove(buf + from, buf + to, len - to);
    len -= to - from;
    cur = from;
    anchor = -1;
    keep_column();
}

/* Removes the selected text. Returns 1 when there was some. */
static int remove_selection(void)
{
    size_t a, b;
    if (!sdk_edit_selection(&a, &b))
        return 0;
    remove_range(a, b);
    return 1;
}

void sdk_edit_anchor(int on)
{
    if (!on)
        anchor = -1;
    else if (anchor < 0)
        anchor = (long)cur;
}

void sdk_edit_select_all(void)
{
    anchor = 0;
    cur = len;
    keep_column();
}

/* ---- typing ---- */

int sdk_edit_type(int c)
{
    int changed = 0;
    if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c <= 126))
        changed = remove_selection();                   /* typing replaces the selected text */
    if (c == '\r' || c == '\n')
        changed |= insert('\n');
    else if (c == '\t') {
        int col = (int)(cur - line_start(cur));
        do
            changed |= insert(' ');
        while (++col % 4);
    } else if (c >= 32 && c <= 126)
        changed |= insert((char)c);
    if (changed)
        keep_column();
    return changed;
}

int sdk_edit_backspace(void)
{
    if (remove_selection())
        return 1;
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
    if (remove_selection())
        return 1;
    if (cur >= len)
        return 0;
    memmove(buf + cur, buf + cur + 1, len - cur - 1);
    len--;
    return 1;
}

/* ---- moves ---- */

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

static int is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

void sdk_edit_word_left(void)
{
    if (cur == line_start(cur)) {
        if (cur > 0)
            cur--;                                      /* the end of the line above */
    } else {
        while (cur > 0 && buf[cur - 1] != '\n' && !is_word(buf[cur - 1]))
            cur--;
        while (cur > 0 && is_word(buf[cur - 1]))
            cur--;
    }
    keep_column();
}

void sdk_edit_word_right(void)
{
    if (cur >= line_end(cur)) {
        if (cur < len)
            cur++;                                      /* the start of the line below */
    } else {
        while (cur < len && is_word(buf[cur]))
            cur++;
        while (cur < len && buf[cur] != '\n' && !is_word(buf[cur]))
            cur++;
    }
    keep_column();
}

void sdk_edit_doc_start(void)
{
    cur = 0;
    keep_column();
}

void sdk_edit_doc_end(void)
{
    cur = len;
    keep_column();
}

void sdk_edit_page_up(int lines)
{
    while (lines-- > 0)
        sdk_edit_up();
}

void sdk_edit_page_down(int lines)
{
    while (lines-- > 0)
        sdk_edit_down();
}

/* ---- lines ---- */

size_t sdk_edit_line_offset(int line)
{
    size_t p = 0;
    for (int l = 0; l < line && p < len; l++) {
        p = line_end(p);
        if (p < len)
            p++;
    }
    return p;
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

/* ---- clipboard ---- */

size_t sdk_edit_clip_length(void) { return clip_len; }

int sdk_edit_copy(int cut)
{
    size_t a, b;
    if (sdk_edit_selection(&a, &b)) {
        memcpy(clip, buf + a, b - a);
        clip_len = b - a;
        if (cut)
            remove_range(a, b);
        return (int)clip_len;
    }
    const size_t s = line_start(cur), e = line_end(cur);
    memcpy(clip, buf + s, e - s);
    clip[e - s] = '\n';
    clip_len = e - s + 1;
    if (cut) {
        if (e < len)
            remove_range(s, e + 1);
        else if (s > 0)
            remove_range(s - 1, e);                     /* the last line: the '\n' above goes too */
        else
            remove_range(s, e);
        cur = line_start(cur);
        keep_column();
    }
    return (int)clip_len;
}

int sdk_edit_paste(void)
{
    if (clip_len == 0)
        return 0;
    remove_selection();
    int n = 0;
    for (size_t i = 0; i < clip_len; i++) {
        if (clip[i] == '\r')
            continue;
        if (!insert(clip[i]))
            break;
        n++;
    }
    keep_column();
    return n;
}

int sdk_edit_duplicate_line(void)
{
    const size_t s = line_start(cur), e = line_end(cur), n = e - s;
    if (len + n + 1 > SDK_EDIT_MAX)
        return 0;
    memmove(buf + e + 1 + n, buf + e, len - e);         /* room for the '\n' and the copy */
    buf[e] = '\n';
    memmove(buf + e + 1, buf + s, n);
    len += n + 1;
    cur += n + 1;                                       /* the cursor goes with the copy */
    anchor = -1;
    keep_column();
    return 1;
}
