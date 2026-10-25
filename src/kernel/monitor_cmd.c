/*
 * The Monitor's command core: see monitor_cmd.h. No hardware and no kprintf
 * here, so the PC can test it (tests/kernel/test_monitor_cmd.c).
 */
#include "monitor_cmd.h"

#include <string.h>

/* ------------------------------------------------------------- text out */

typedef struct {
    char *p;
    size_t n, cap;
} tb_t;

static void tb_init(tb_t *b, char *p, size_t cap)
{
    b->p = p;
    b->n = 0;
    b->cap = cap;
    if (cap)
        p[0] = 0;
}

static void tb_str(tb_t *b, const char *s)
{
    while (*s && b->n + 1 < b->cap)
        b->p[b->n++] = *s++;
    if (b->cap)
        b->p[b->n] = 0;
}

static void tb_pad(tb_t *b, size_t from, size_t width)
{
    while (b->n - from < width && b->n + 1 < b->cap)
        b->p[b->n++] = ' ';
    if (b->cap)
        b->p[b->n] = 0;
}

/* ------------------------------------------------------------ tokenizer */

int mcmd_split(char **pp, char **argv, int max, int *argcp)
{
    char *p = *pp;
    int argc = 0;

    *argcp = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p) {
            *pp = NULL;
            break;
        }
        if (*p == ';') {
            *pp = p + 1;
            break;
        }
        if (argc >= max)
            return MCMD_E_ARGS;
        char *o = p, q = 0;
        argv[argc++] = p;
        while (*p) {
            char c = *p;
            if (q) {
                if (c == q) {
                    q = 0;
                    p++;
                } else if (c == '\\' && q == '"' && p[1]) {
                    p++;
                    *o++ = *p++;
                } else {
                    *o++ = c;
                    p++;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                q = c;
                p++;
            } else if (c == '\\' && p[1]) {
                p++;
                *o++ = *p++;
            } else if (c == ' ' || c == '\t' || c == ';') {
                break;
            } else {
                *o++ = c;
                p++;
            }
        }
        if (q)
            return MCMD_E_QUOTE;
        char delim = *p;
        *o = 0;
        if (delim == ';') {
            *pp = p + 1;
            break;
        }
        if (delim)
            p++;
        else {
            *pp = NULL;
            break;
        }
    }
    *argcp = argc;
    return 0;
}

/* ------------------------------------------------------------- dispatcher */

/* the words of an alias ("bench 3d") copied into buf; their count */
static int alias_words(const char *alias, char *buf, size_t cap, char **w, int max)
{
    int n = 0;
    size_t l = strlen(alias);

    if (l >= cap)
        return 0;
    memcpy(buf, alias, l + 1);
    for (char *p = buf; *p && n < max;) {
        while (*p == ' ')
            *p++ = 0;
        if (!*p)
            break;
        w[n++] = p;
        while (*p && *p != ' ')
            p++;
    }
    return n;
}

const mcmd_t *mcmd_find(const mcmd_t *tab, const char *name)
{
    for (const mcmd_t *r = tab; r && r->name; r++)
        if (!strcmp(r->name, name))
            return r;
    return NULL;
}

int mcmd_exec(const mcmd_t *tab, int argc, char **argv, const mcmd_t **where, const char **word)
{
    char abuf[48];
    char *av[MCMD_MAX_ARGS + 4];
    const mcmd_t *row;

    *where = NULL;
    *word = argc > 0 ? argv[0] : "";
    if (argc <= 0)
        return 0;
    row = mcmd_find(tab, argv[0]);
    if (!row)
        return MCMD_E_UNKNOWN;
    if (row->alias) {                   /* the old word: its words, then the arguments */
        int n = alias_words(row->alias, abuf, sizeof abuf, av, 4);
        if (n == 0)
            return MCMD_E_UNKNOWN;
        for (int i = 1; i < argc && n < MCMD_MAX_ARGS + 4; i++)
            av[n++] = argv[i];
        argc = n;
        argv = av;
        row = mcmd_find(tab, argv[0]);
        if (!row)
            return MCMD_E_UNKNOWN;
    }
    while (row->sub && argc > 1) {
        const mcmd_t *s = mcmd_find(row->sub, argv[1]);
        if (!s) {
            *where = row;
            *word = argv[1];
            return MCMD_E_SUBCMD;
        }
        row = s;
        argv++;
        argc--;
    }
    if (!row->fn) {
        *where = row;
        return MCMD_E_NOFN;
    }
    return row->fn(argc, argv);
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* edit distance of two short words, or 99 when they are long */
static int distance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    int d[20][20];
    if (la > 18 || lb > 18)
        return 99;
    for (int i = 0; i <= la; i++)
        d[i][0] = i;
    for (int j = 0; j <= lb; j++)
        d[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int c = lower(a[i - 1]) != lower(b[j - 1]);
            int m = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < m)
                m = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + c < m)
                m = d[i - 1][j - 1] + c;
            if (i > 1 && j > 1 && lower(a[i - 1]) == lower(b[j - 2]) && lower(a[i - 2]) == lower(b[j - 1]) &&
                d[i - 2][j - 2] + 1 < m)
                m = d[i - 2][j - 2] + 1;    /* two letters swapped */
            d[i][j] = m;
        }
    return d[la][lb];
}

int mcmd_suggest(const mcmd_t *tab, const char *word, char *out, size_t cap)
{
    tb_t b;
    int n = 0, best = 3;
    size_t wl = strlen(word);

    tb_init(&b, out, cap);
    for (const mcmd_t *r = tab; r && r->name; r++)       /* a word that starts like it */
        if (wl > 0 && !strncmp(r->name, word, wl) && n < 4) {
            if (n++)
                tb_str(&b, ", ");
            tb_str(&b, r->name);
        }
    if (n)
        return n;
    for (const mcmd_t *r = tab; r && r->name; r++)       /* else the nearest ones, same first letter */
        if (lower(r->name[0]) == lower(word[0])) {
            int d = distance(r->name, word);
            if (d < best)
                best = d;
        }
    if (best > 2)
        return 0;
    for (const mcmd_t *r = tab; r && r->name && n < 4; r++)
        if (lower(r->name[0]) == lower(word[0]) && distance(r->name, word) == best) {
            if (n++)
                tb_str(&b, ", ");
            tb_str(&b, r->name);
        }
    return n;
}

void mcmd_error_text(const mcmd_t *tab, int err, const mcmd_t *where, const char *word, char *out,
                     size_t cap)
{
    tb_t b;
    char sug[96];

    tb_init(&b, out, cap);
    switch (err) {
    case MCMD_E_UNKNOWN:
        tb_str(&b, "'");
        tb_str(&b, word);
        tb_str(&b, "': not a command of the line");
        if (mcmd_suggest(tab, word, sug, sizeof sug)) {
            tb_str(&b, "; did you mean: ");
            tb_str(&b, sug);
        }
        tb_str(&b, " ('help' lists the commands)");
        break;
    case MCMD_E_SUBCMD:
    case MCMD_E_NOFN:
        tb_str(&b, "'");
        tb_str(&b, where ? where->name : "?");
        tb_str(&b, err == MCMD_E_SUBCMD ? "': not a subcommand: '" : "': needs one of: ");
        if (err == MCMD_E_SUBCMD) {
            tb_str(&b, word);
            tb_str(&b, "'; use: ");
        }
        for (const mcmd_t *s = where ? where->sub : NULL; s && s->name; s++) {
            tb_str(&b, s->name);
            if ((s + 1)->name)
                tb_str(&b, ", ");
        }
        break;
    case MCMD_E_QUOTE:
        tb_str(&b, "a quote is not closed");
        break;
    case MCMD_E_ARGS:
        tb_str(&b, "too many words in one command");
        break;
    default:
        tb_str(&b, "error");
    }
}

int mcmd_run_line(const mcmd_t *tab, char *line, const mcmd_hooks_t *h)
{
    char *argv[MCMD_MAX_ARGS];
    char msg[220];
    char *p = line;

    while (p) {
        int argc, r = mcmd_split(&p, argv, MCMD_MAX_ARGS, &argc);
        const mcmd_t *where = NULL;
        const char *word = "";

        if (r == 0 && argc > 0) {
            if (h && h->before)
                h->before(argc, argv);
            r = mcmd_exec(tab, argc, argv, &where, &word);
        }
        if (r >= MCMD_E_ARGS && r <= MCMD_E_UNKNOWN) {
            mcmd_error_text(tab, r, where, word, msg, sizeof msg);
            if (h && h->error)
                h->error(msg);
        }
        if (r != 0)
            return -1;
    }
    return 0;
}

/* ---- the key shortcuts ---- */

const mcmd_t *mcmd_find_key(const mcmd_t *tab, int key, char *path, size_t cap)
{
    for (const mcmd_t *r = tab; r && r->name; r++) {
        if (r->alias)
            continue;
        if (r->key && r->key == key) {
            if (cap)
                path[0] = 0;
            tb_t b;
            tb_init(&b, path, cap);
            tb_str(&b, r->name);
            return r;
        }
        if (r->sub) {
            const mcmd_t *s = mcmd_find_key(r->sub, key, path, cap);
            if (s) {
                char tmp[48];
                tb_t b;
                tb_init(&b, tmp, sizeof tmp);
                tb_str(&b, r->name);
                tb_str(&b, " ");
                tb_str(&b, path);
                tb_init(&b, path, cap);
                tb_str(&b, tmp);
                return s;
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ help */

#define HELP_USAGE_COL 26           /* width of the "words and arguments" field */
#define HELP_TEXT_COL 32            /* where the help text starts */

static void help_row(tb_t *b, const mcmd_t *r, const char *path)
{
    char usage[96];
    tb_t u;

    tb_init(&u, usage, sizeof usage);
    tb_str(&u, path);
    if (r->args && r->args[0]) {
        tb_str(&u, " ");
        tb_str(&u, r->args);
    }
    tb_str(b, "  ");
    tb_str(b, r->key ? (char[]){ r->key, 0 } : " ");
    tb_str(b, "  ");
    size_t from = b->n;
    tb_str(b, usage);
    if (u.n > HELP_USAGE_COL - 1) {
        tb_str(b, "\n");
        from = b->n;
        tb_pad(b, from, HELP_TEXT_COL);
    } else {
        tb_pad(b, from, HELP_USAGE_COL);
        tb_str(b, " ");
    }
    tb_str(b, r->help);
    tb_str(b, "\n");
}

static void help_walk(tb_t *b, const mcmd_t *tab, const char *prefix)
{
    for (const mcmd_t *r = tab; r && r->name; r++) {
        char path[64];
        tb_t p;

        if (r->alias)
            continue;
        tb_init(&p, path, sizeof path);
        tb_str(&p, prefix);
        tb_str(&p, r->name);
        if (r->help && (r->fn || !r->sub))
            help_row(b, r, path);
        if (r->sub) {
            tb_str(&p, " ");
            help_walk(b, r->sub, path);
        }
    }
}

size_t mcmd_help_all(const mcmd_t *tab, const char *head, const char *foot, char *out, size_t cap)
{
    tb_t b;
    int first = 1;

    tb_init(&b, out, cap);
    if (head)
        tb_str(&b, head);
    help_walk(&b, tab, "");
    size_t line = b.n;                  /* the start of the line of the old words: it wraps at 76 */
    for (const mcmd_t *r = tab; r && r->name; r++)
        if (r->alias) {
            size_t need = strlen(r->name) + strlen(r->alias) + 3;
            if (first) {
                tb_str(&b, "old words that still work:");
                first = 0;
            }
            if (b.n - line + need > 76) {
                tb_str(&b, "\n  ");
                line = b.n - 2;
            }
            tb_str(&b, " ");
            tb_str(&b, r->name);
            tb_str(&b, "=");
            tb_str(&b, r->alias);
            tb_str(&b, ",");
        }
    if (!first) {
        if (b.n > 0 && b.p[b.n - 1] == ',')
            b.p[--b.n] = 0;
        tb_str(&b, "\n");
    }
    if (foot)
        tb_str(&b, foot);
    return b.n;
}

size_t mcmd_help_one(const mcmd_t *tab, int argc, char **argv, char *out, size_t cap)
{
    tb_t b;
    const mcmd_t *t = tab, *row = NULL;
    char path[64];
    tb_t p;

    tb_init(&b, out, cap);
    tb_init(&p, path, sizeof path);
    for (int i = 0; i < argc; i++) {
        const mcmd_t *r = mcmd_find(t, argv[i]);
        if (!r)
            return 0;
        if (r->alias) {                 /* help for an old word: the help of the new one */
            char a[48];
            char *w[4];
            int n = alias_words(r->alias, a, sizeof a, w, 4);
            tb_str(&b, argv[i]);
            tb_str(&b, " is the old word for '");
            tb_str(&b, r->alias);
            tb_str(&b, "':\n");
            return b.n + mcmd_help_one(tab, n, w, out + b.n, cap - b.n);
        }
        if (i)
            tb_str(&p, " ");
        tb_str(&p, r->name);
        row = r;
        t = r->sub;
        if (!t && i + 1 < argc)
            return 0;
    }
    if (!row)
        return 0;
    if (row->help && (row->fn || !row->sub)) {
        help_row(&b, row, path);
    } else if (row->help) {
        tb_str(&b, "  ");
        tb_str(&b, path);
        tb_str(&b, ": ");
        tb_str(&b, row->help);
        tb_str(&b, "\n");
    }
    if (row->sub) {
        tb_str(&p, " ");
        help_walk(&b, row->sub, path);
    }
    return b.n;
}

/* ---------------------------------------------------------------- editor */

static void eput(medit_t *e, const char *s)
{
    if (e->out)
        e->out(s);
}

static void eput_c(medit_t *e, char c)
{
    char s[2] = { c, 0 };
    eput(e, s);
}

void medit_init(medit_t *e, const mcmd_t *tab, medit_out_fn out)
{
    memset(e, 0, sizeof *e);
    e->tab = tab;
    e->out = out;
    e->browse = -1;
}

void medit_redraw(medit_t *e)
{
    eput(e, "\r");
    eput(e, e->prompt ? e->prompt : "");
    eput(e, e->buf);
    eput(e, "\x1b[K");
    for (int i = e->len; i > e->pos; i--)
        eput(e, "\b");
}

void medit_begin(medit_t *e, const char *prompt, int show)
{
    e->prompt = prompt;
    e->buf[0] = 0;
    e->len = e->pos = 0;
    e->esc = 0;
    e->browse = -1;
    if (show)
        eput(e, prompt);
}

const char *medit_text(const medit_t *e)
{
    return e->buf;
}

int medit_in_escape(const medit_t *e)
{
    return e->esc == 1;
}

const char *medit_history(const medit_t *e, int i)
{
    if (i < 0 || i >= e->hist_n)
        return NULL;
    return e->hist[(e->hist_head - 1 - i + 2 * MEDIT_HIST) % MEDIT_HIST];
}

static void hist_add(medit_t *e)
{
    if (!e->buf[0])
        return;
    const char *last = medit_history(e, 0);
    if (last && !strcmp(last, e->buf))
        return;
    strcpy(e->hist[e->hist_head], e->buf);
    e->hist_head = (e->hist_head + 1) % MEDIT_HIST;
    if (e->hist_n < MEDIT_HIST)
        e->hist_n++;
}

static void set_text(medit_t *e, const char *s)
{
    strncpy(e->buf, s, MEDIT_LINE_MAX - 1);
    e->buf[MEDIT_LINE_MAX - 1] = 0;
    e->len = e->pos = (int)strlen(e->buf);
}

static void insert(medit_t *e, const char *s)
{
    int n = (int)strlen(s);
    if (e->len + n >= MEDIT_LINE_MAX)
        return;
    memmove(e->buf + e->pos + n, e->buf + e->pos, (size_t)(e->len - e->pos + 1));
    memcpy(e->buf + e->pos, s, (size_t)n);
    e->len += n;
    e->pos += n;
}

static void delete_range(medit_t *e, int from, int to)
{
    memmove(e->buf + from, e->buf + to, (size_t)(e->len - to + 1));
    e->len -= to - from;
    e->pos = from;
}

/* ---- Tab */

#define CAND_MAX 96

/* the row of a first word; an alias gives the row it stands for */
static const mcmd_t *resolve(const mcmd_t *tab, const char *name)
{
    const mcmd_t *r = mcmd_find(tab, name);
    if (r && r->alias) {
        char a[48];
        char *w[4];
        int n = alias_words(r->alias, a, sizeof a, w, 4);
        r = n ? mcmd_find(tab, w[0]) : NULL;
        for (int i = 1; r && r->sub && i < n; i++)
            r = mcmd_find(r->sub, w[i]);
    }
    return r;
}

/* the words that can follow what is before the cursor, starting like part */
static int collect(medit_t *e, const char *part, size_t pl, const char **c, int max)
{
    char head[MEDIT_LINE_MAX];
    int n = 0, nw = 0;
    const char *words[MCMD_MAX_ARGS];
    size_t start = (size_t)e->pos - pl;

    memcpy(head, e->buf, start);
    head[start] = 0;
    for (char *p = head; *p;) {
        while (*p == ' ')
            *p++ = 0;
        if (!*p)
            break;
        if (nw >= MCMD_MAX_ARGS)
            return 0;
        words[nw++] = p;
        while (*p && *p != ' ')
            p++;
    }
    if (nw == 0) {
        for (const mcmd_t *r = e->tab; r && r->name && n < max; r++)
            if (!strncmp(r->name, part, pl))
                c[n++] = r->name;
        return n;
    }
    const mcmd_t *row = resolve(e->tab, words[0]);
    for (int i = 1; row && i < nw; i++)
        row = row->sub ? mcmd_find(row->sub, words[i]) : NULL;
    if (!row)
        return 0;
    if (row->sub)
        for (const mcmd_t *s = row->sub; s->name && n < max; s++)
            if (!s->alias && !strncmp(s->name, part, pl))
                c[n++] = s->name;
    if (row->cand && !strchr(part, '='))
        for (int i = 0; n < max; i++) {
            const char *w = row->cand(i);
            if (!w)
                break;
            if (!strncmp(w, part, pl))
                c[n++] = w;
        }
    return n;
}

static void complete(medit_t *e)
{
    const char *c[CAND_MAX];
    int ps = e->pos;

    while (ps > 0 && e->buf[ps - 1] != ' ')
        ps--;
    size_t pl = (size_t)(e->pos - ps);
    char part[MEDIT_LINE_MAX];
    memcpy(part, e->buf + ps, pl);
    part[pl] = 0;
    if (strpbrk(part, "\"'\\;"))
        return;
    int n = collect(e, part, pl, c, CAND_MAX);
    if (n == 0)
        return;
    /* the longest start that all candidates share */
    size_t common = strlen(c[0]);
    for (int i = 1; i < n; i++) {
        size_t k = 0;
        while (k < common && c[i][k] == c[0][k])
            k++;
        common = k;
    }
    if (common > pl || n == 1) {
        char add[MEDIT_LINE_MAX];
        memcpy(add, c[0], common);
        add[common] = 0;
        if (n == 1 && (common == 0 || add[common - 1] != '='))
            strcat(add, " ");
        delete_range(e, ps, e->pos);
        insert(e, add);
        medit_redraw(e);
        return;
    }
    /* several words and nothing more to add: the list, then the line again */
    eput(e, "\n");
    size_t col = 0;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(c[i]);
        if (col && col + l + 2 > 76) {
            eput(e, "\n");
            col = 0;
        }
        eput(e, c[i]);
        eput(e, "  ");
        col += l + 2;
    }
    eput(e, "\n");
    medit_redraw(e);
}

/* ---- keys */

static void history_move(medit_t *e, int dir)
{
    if (dir < 0) {                      /* Up: an older line */
        if (e->browse + 1 >= e->hist_n)
            return;
        if (e->browse < 0)
            strcpy(e->saved, e->buf);
        e->browse++;
        set_text(e, medit_history(e, e->browse));
    } else {
        if (e->browse < 0)
            return;
        e->browse--;
        set_text(e, e->browse < 0 ? e->saved : medit_history(e, e->browse));
    }
    medit_redraw(e);
}

static int word_start(const medit_t *e, int from)
{
    int i = from;
    while (i > 0 && e->buf[i - 1] == ' ')
        i--;
    while (i > 0 && e->buf[i - 1] != ' ')
        i--;
    return i;
}

static int do_key(medit_t *e, int k)
{
    switch (k) {
    case MEDIT_K_ESC:
        eput(e, "\n");
        return MEDIT_EXIT;
    case MEDIT_K_UP:
        history_move(e, -1);
        return MEDIT_NONE;
    case MEDIT_K_DOWN:
        history_move(e, 1);
        return MEDIT_NONE;
    case MEDIT_K_LEFT:
    case 2:                             /* Ctrl-B */
        if (e->pos > 0) {
            e->pos--;
            eput(e, "\b");
        }
        return MEDIT_NONE;
    case MEDIT_K_RIGHT:
    case 6:                             /* Ctrl-F */
        if (e->pos < e->len)
            eput_c(e, e->buf[e->pos++]);
        return MEDIT_NONE;
    case MEDIT_K_HOME:
    case 1:                             /* Ctrl-A */
        e->pos = 0;
        medit_redraw(e);
        return MEDIT_NONE;
    case MEDIT_K_END:
    case 5:                             /* Ctrl-E */
        e->pos = e->len;
        medit_redraw(e);
        return MEDIT_NONE;
    case MEDIT_K_DEL:
        if (e->pos < e->len) {
            delete_range(e, e->pos, e->pos + 1);
            medit_redraw(e);
        }
        return MEDIT_NONE;
    case 4:                             /* Ctrl-D: leaves on an empty line, else Del */
        if (e->len == 0) {
            eput(e, "\n");
            return MEDIT_EXIT;
        }
        return do_key(e, MEDIT_K_DEL);
    case 3:                             /* Ctrl-C */
        eput(e, "^C\n");
        e->buf[0] = 0;
        e->len = e->pos = 0;
        return MEDIT_CANCEL;
    case 21:                            /* Ctrl-U: what is before the cursor */
        delete_range(e, 0, e->pos);
        medit_redraw(e);
        return MEDIT_NONE;
    case 11:                            /* Ctrl-K: what is after the cursor */
        e->buf[e->pos] = 0;
        e->len = e->pos;
        medit_redraw(e);
        return MEDIT_NONE;
    case 23: {                          /* Ctrl-W: the word before the cursor */
        int s = word_start(e, e->pos);
        delete_range(e, s, e->pos);
        medit_redraw(e);
        return MEDIT_NONE;
    }
    case 0x7F:
    case 8:
        if (e->pos > 0) {
            if (e->pos == e->len) {
                e->buf[--e->len] = 0;
                e->pos--;
                eput(e, "\b \b");
            } else {
                delete_range(e, e->pos - 1, e->pos);
                medit_redraw(e);
            }
        }
        return MEDIT_NONE;
    case '\t':
        complete(e);
        return MEDIT_NONE;
    case '\r':
    case '\n':
        eput(e, "\n");
        hist_add(e);
        e->browse = -1;
        return MEDIT_LINE;
    default:
        break;
    }
    if (k >= 32 && k < 127) {
        char s[2] = { (char)k, 0 };
        if (e->len + 1 >= MEDIT_LINE_MAX)
            return MEDIT_NONE;
        int at_end = e->pos == e->len;
        insert(e, s);
        if (at_end)
            eput(e, s);
        else
            medit_redraw(e);
    }
    return MEDIT_NONE;
}

static int csi_final(medit_t *e, int c)
{
    switch (c) {
    case 'A': return do_key(e, MEDIT_K_UP);
    case 'B': return do_key(e, MEDIT_K_DOWN);
    case 'C': return do_key(e, MEDIT_K_RIGHT);
    case 'D': return do_key(e, MEDIT_K_LEFT);
    case 'H': return do_key(e, MEDIT_K_HOME);
    case 'F': return do_key(e, MEDIT_K_END);
    case '~':
        if (e->esc_num == 1 || e->esc_num == 7)
            return do_key(e, MEDIT_K_HOME);
        if (e->esc_num == 4 || e->esc_num == 8)
            return do_key(e, MEDIT_K_END);
        if (e->esc_num == 3)
            return do_key(e, MEDIT_K_DEL);
        return MEDIT_NONE;
    default:
        return MEDIT_NONE;
    }
}

int medit_feed(medit_t *e, int k)
{
    if (k < 0)
        return MEDIT_NONE;
    if (k == MEDIT_K_ESC) {
        e->esc = 0;
        return do_key(e, k);
    }
    if (k >= MEDIT_K_UP) {              /* a key the keyboard gave as it is */
        e->esc = 0;
        e->last_cr = 0;
        return do_key(e, k);
    }
    switch (e->esc) {
    case 1:                             /* after ESC */
        if (k == '[') {
            e->esc = 2;
            e->esc_num = 0;
        } else if (k == 'O') {
            e->esc = 3;
        } else {
            e->esc = 0;                 /* Alt + a key: both dropped */
        }
        return MEDIT_NONE;
    case 2:                             /* ESC [ parameters, then the final byte */
        if (k >= '0' && k <= '9') {
            e->esc_num = e->esc_num * 10 + (k - '0');
            return MEDIT_NONE;
        }
        if (k == ';' || (k >= 0x20 && k <= 0x3F))
            return MEDIT_NONE;
        e->esc = 0;
        if (k >= 0x40 && k <= 0x7E)
            return csi_final(e, k);
        return MEDIT_NONE;
    case 3:                             /* ESC O letter */
        e->esc = 0;
        return csi_final(e, k);
    default:
        break;
    }
    if (k == 0x1B) {
        e->esc = 1;
        return MEDIT_NONE;
    }
    if (k == '\n' && e->last_cr) {      /* "\r\n" is one Enter */
        e->last_cr = 0;
        return MEDIT_NONE;
    }
    e->last_cr = k == '\r';
    return do_key(e, k);
}
