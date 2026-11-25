/*
 * Shell SDK: the text buffer of the Code tab (see shell_sdk_edit.h). Original code.
 */
#include "shell_sdk_edit.h"

#include <stdlib.h>
#include <string.h>

static char buf[SDK_EDIT_MAX];
static size_t len;
static size_t cur;                      /* the cursor: an offset in buf */
static int want;                        /* the column that Up and Down try to keep */
static long anchor = -1;                /* the other end of the selection (-1: none) */
static char clip[SDK_EDIT_MAX + 1];
static size_t clip_len;
static void hist_clear(void);
static unsigned rev;                    /* grows with each change of the text */

void sdk_edit_reset(void)
{
    len = cur = 0;
    want = 0;
    anchor = -1;
    hist_clear();
}

size_t sdk_edit_length(void) { return len; }
unsigned sdk_edit_revision(void) { return rev; }

void sdk_edit_set(const char *text, size_t n)
{
    len = 0;
    for (size_t i = 0; i < n && len < SDK_EDIT_MAX; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == 13)                                    /* a carriage return */
            continue;
        buf[len++] = c == 10 || (c >= 32 && c <= 126) ? (char)c : ' ';
    }
    cur = 0;
    want = 0;
    anchor = -1;
    rev++;
    hist_clear();
}

void sdk_edit_goto_line(int line)
{
    anchor = -1;
    cur = sdk_edit_line_offset(line > 1 ? line - 1 : 0);
    want = 0;
}

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
    rev++;
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
    rev++;
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

static int type_do(int c)
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

static int backspace_do(void)
{
    if (remove_selection())
        return 1;
    if (cur == 0)
        return 0;
    memmove(buf + cur - 1, buf + cur, len - cur);
    cur--;
    len--;
    rev++;
    keep_column();
    return 1;
}

static int delete_do(void)
{
    if (remove_selection())
        return 1;
    if (cur >= len)
        return 0;
    memmove(buf + cur, buf + cur + 1, len - cur - 1);
    len--;
    rev++;
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

void sdk_edit_clip_set(const char *text, size_t len)
{
    clip_len = len < SDK_EDIT_MAX ? len : SDK_EDIT_MAX;
    memcpy(clip, text, clip_len);
}

static int copy_do(int cut)
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

static int paste_do(void)
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

static int duplicate_do(void)
{
    const size_t s = line_start(cur), e = line_end(cur), n = e - s;
    if (len + n + 1 > SDK_EDIT_MAX)
        return 0;
    memmove(buf + e + 1 + n, buf + e, len - e);         /* room for the '\n' and the copy */
    buf[e] = '\n';
    memmove(buf + e + 1, buf + s, n);
    len += n + 1;
    rev++;
    cur += n + 1;                                       /* the cursor goes with the copy */
    anchor = -1;
    keep_column();
    return 1;
}

/* ---- undo and redo: a snapshot of the text before each step ---- */

typedef struct { char *data; size_t len, cur; } snap_t;
#define UNDO_DEPTH 100
static snap_t undo_st[UNDO_DEPTH], redo_st[UNDO_DEPTH];
static int undo_n, redo_n;
static int last_kind;                   /* the kind of the last step (0: it does not merge with the next) */
static size_t last_cur;                 /* where the cursor was when that step ended */

static void stack_clear(snap_t *st, int *n)
{
    while (*n > 0)
        free(st[--*n].data);
}

static void hist_clear(void)
{
    stack_clear(undo_st, &undo_n);
    stack_clear(redo_st, &redo_n);
    last_kind = 0;
}

static int snap_take(snap_t *s)
{
    s->data = malloc(len ? len : 1);
    if (!s->data)
        return 0;
    memcpy(s->data, buf, len);
    s->len = len;
    s->cur = cur;
    return 1;
}

static void snap_put(const snap_t *s)
{
    memcpy(buf, s->data, s->len);
    len = s->len;
    cur = s->cur;
    anchor = -1;
    rev++;
    keep_column();
}

/* Before a change of the text. A kind (1 typing, 2 Backspace, 3 Delete) merges with the step before it when the
 * cursor has not moved since: a word typed in a row is one step. Kind 0 is always a step of its own. */
static void edit_begin(int kind)
{
    stack_clear(redo_st, &redo_n);
    if (kind && kind == last_kind && cur == last_cur && undo_n > 0)
        return;
    if (undo_n == UNDO_DEPTH) {                         /* the oldest step goes */
        free(undo_st[0].data);
        memmove(undo_st, undo_st + 1, (UNDO_DEPTH - 1) * sizeof undo_st[0]);
        undo_n--;
    }
    if (snap_take(&undo_st[undo_n]))
        undo_n++;
    last_kind = kind;
}

static void edit_end(void) { last_cur = cur; }

int sdk_edit_undo(void)
{
    if (undo_n == 0)
        return 0;
    if (redo_n < UNDO_DEPTH && snap_take(&redo_st[redo_n]))
        redo_n++;
    snap_t s = undo_st[--undo_n];
    snap_put(&s);
    free(s.data);
    last_kind = 0;
    return 1;
}

int sdk_edit_redo(void)
{
    if (redo_n == 0)
        return 0;
    if (undo_n < UNDO_DEPTH && snap_take(&undo_st[undo_n]))
        undo_n++;
    snap_t s = redo_st[--redo_n];
    snap_put(&s);
    free(s.data);
    last_kind = 0;
    return 1;
}

/* ---- the changes of the text, each one a step of the history ---- */

int sdk_edit_type(int c)
{
    if (!(c == 13 || c == 10 || c == 9 || (c >= 32 && c <= 126)))
        return 0;
    size_t a, b;
    edit_begin(c >= 32 && !sdk_edit_selection(&a, &b) ? 1 : 0);
    const int changed = type_do(c);
    edit_end();
    return changed;
}

int sdk_edit_backspace(void)
{
    size_t a, b;
    const int sel = sdk_edit_selection(&a, &b);
    if (!sel && cur == 0)
        return 0;
    edit_begin(sel ? 0 : 2);
    const int changed = backspace_do();
    edit_end();
    return changed;
}

int sdk_edit_delete(void)
{
    size_t a, b;
    const int sel = sdk_edit_selection(&a, &b);
    if (!sel && cur >= len)
        return 0;
    edit_begin(sel ? 0 : 3);
    const int changed = delete_do();
    edit_end();
    return changed;
}

int sdk_edit_copy(int cut)
{
    if (cut)
        edit_begin(0);
    const int n = copy_do(cut);
    edit_end();
    return n;
}

int sdk_edit_paste(void)
{
    if (clip_len == 0)
        return 0;
    edit_begin(0);
    const int n = paste_do();
    edit_end();
    return n;
}

int sdk_edit_replace(size_t from, size_t to, const char *text, size_t n)
{
    if (from > to || to > len || len - (to - from) + n > SDK_EDIT_MAX)
        return -1;
    if (to - from == n && !memcmp(buf + from, text, n))
        return 0;                                       /* no change: no step, nothing to save */
    edit_begin(0);
    memmove(buf + from + n, buf + to, len - to);
    memcpy(buf + from, text, n);
    len = len - (to - from) + n;
    if (cur >= to)
        cur = cur - (to - from) + n;
    else if (cur > from)
        cur = from;
    anchor = -1;
    rev++;
    keep_column();
    edit_end();
    return 0;
}

int sdk_edit_duplicate_line(void)
{
    edit_begin(0);
    const int changed = duplicate_do();
    edit_end();
    return changed;
}
