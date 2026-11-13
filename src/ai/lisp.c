/*
 * Minimal arena-allocated Lisp engine for symbolic code synthesis.
 * Pure C99, zero external dependencies, deterministic memory & step budget.
 */
#include "lisp.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LISP_MAX_STEPS   300000
#define LISP_MAX_DEPTH   256
#define LISP_SYM_BUCKETS 256

typedef enum {
    V_NIL = 0,
    V_BOOL,
    V_INT,
    V_SYM,
    V_STR,
    V_CONS,
    V_PRIM,
    V_CLOSURE,
    V_MACRO
} val_tag_t;

typedef struct val val_t;
typedef val_t *(*prim_fn_t)(bm_lisp_t *L, val_t *args);

struct val {
    val_tag_t tag;
    union {
        int32_t i;
        struct { const char *s; uint32_t len; uint32_t hash; val_t *next; } sym;
        struct { const char *s; uint32_t len; } str;
        struct { val_t *car; val_t *cdr; } cons;
        struct { const char *name; prim_fn_t fn; } prim;
        struct { val_t *params; val_t *body; val_t *env; } clos;
    } u;
};

struct bm_lisp {
    uint8_t *base;
    size_t cap;
    size_t used;
    size_t base_used;

    val_t *nil_val;
    val_t *true_val;
    val_t *false_val;
    val_t *sym_buckets[LISP_SYM_BUCKETS];
    val_t *base_buckets[LISP_SYM_BUCKETS];

    val_t *global_env;
    val_t *base_env;

    /* Common interned symbols */
    val_t *s_quote, *s_if, *s_cond, *s_else, *s_define, *s_set, *s_lambda;
    val_t *s_let, *s_lets, *s_begin, *s_do, *s_and, *s_or, *s_defmacro, *s_match;
    val_t *s_quasiquote, *s_unquote, *s_unquote_splicing, *s_wild;

    int steps;
    int depth;
    int has_err;
    char err[256];
};

static void set_err(bm_lisp_t *L, const char *msg)
{
    if (!L->has_err) {
        L->has_err = 1;
        snprintf(L->err, sizeof(L->err), "%s", msg ? msg : "lisp error");
    }
}

static void *arena_alloc(bm_lisp_t *L, size_t n)
{
    n = (n + 7u) & ~(size_t)7u;
    if (L->used + n > L->cap) {
        set_err(L, "lisp: out of arena memory");
        return NULL;
    }
    void *p = L->base + L->used;
    L->used += n;
    return p;
}

static val_t *mk_val(bm_lisp_t *L, val_tag_t tag)
{
    val_t *v = (val_t *)arena_alloc(L, sizeof(val_t));
    if (!v) return L->nil_val;
    memset(v, 0, sizeof(*v));
    v->tag = tag;
    return v;
}

static val_t *mk_int(bm_lisp_t *L, int32_t i)
{
    val_t *v = mk_val(L, V_INT);
    v->u.i = i;
    return v;
}

static val_t *mk_bool(bm_lisp_t *L, int b)
{
    return b ? L->true_val : L->false_val;
}

static val_t *mk_str_n(bm_lisp_t *L, const char *s, uint32_t len)
{
    char *buf = (char *)arena_alloc(L, len + 1);
    if (!buf) return L->nil_val;
    if (len) memcpy(buf, s, len);
    buf[len] = '\0';
    val_t *v = mk_val(L, V_STR);
    v->u.str.s = buf;
    v->u.str.len = len;
    return v;
}

static val_t *cons(bm_lisp_t *L, val_t *a, val_t *d)
{
    val_t *v = mk_val(L, V_CONS);
    v->u.cons.car = a;
    v->u.cons.cdr = d;
    return v;
}

static uint32_t hash_str(const char *s, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static val_t *intern_n(bm_lisp_t *L, const char *s, uint32_t len)
{
    uint32_t h = hash_str(s, len);
    uint32_t b = h & (LISP_SYM_BUCKETS - 1);
    for (val_t *p = L->sym_buckets[b]; p; p = p->u.sym.next) {
        if (p->u.sym.hash == h && p->u.sym.len == len && memcmp(p->u.sym.s, s, len) == 0)
            return p;
    }
    char *copy = (char *)arena_alloc(L, len + 1);
    if (!copy) return L->nil_val;
    memcpy(copy, s, len);
    copy[len] = '\0';
    val_t *v = mk_val(L, V_SYM);
    if (L->has_err) return L->nil_val;
    v->u.sym.s = copy;
    v->u.sym.len = len;
    v->u.sym.hash = h;
    v->u.sym.next = L->sym_buckets[b];
    L->sym_buckets[b] = v;
    return v;
}

static val_t *intern(bm_lisp_t *L, const char *s)
{
    return intern_n(L, s, (uint32_t)strlen(s));
}

static int is_truthy(val_t *v)
{
    if (!v || v->tag == V_NIL) return 0;
    if (v->tag == V_BOOL && v->u.i == 0) return 0;
    return 1;
}

static val_t *car(val_t *v) { return (v && v->tag == V_CONS) ? v->u.cons.car : NULL; }
static val_t *cdr(val_t *v) { return (v && v->tag == V_CONS) ? v->u.cons.cdr : NULL; }

/* Environment: assoc list of (sym . val) frames */
static val_t *env_lookup(bm_lisp_t *L, val_t *env, val_t *sym)
{
    for (val_t *e = env; e && e->tag == V_CONS; e = e->u.cons.cdr) {
        val_t *pair = e->u.cons.car;
        if (pair && pair->tag == V_CONS && pair->u.cons.car == sym)
            return pair->u.cons.cdr;
    }
    if (env != L->global_env) {
        for (val_t *e = L->global_env; e && e->tag == V_CONS; e = e->u.cons.cdr) {
            val_t *pair = e->u.cons.car;
            if (pair && pair->tag == V_CONS && pair->u.cons.car == sym)
                return pair->u.cons.cdr;
        }
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "lisp: unbound symbol '%s'",
             (sym && sym->tag == V_SYM) ? sym->u.sym.s : "?");
    set_err(L, msg);
    return L->nil_val;
}

static val_t *env_set(bm_lisp_t *L, val_t *env, val_t *sym, val_t *val)
{
    for (val_t *e = env; e && e->tag == V_CONS; e = e->u.cons.cdr) {
        val_t *pair = e->u.cons.car;
        if (pair && pair->tag == V_CONS && pair->u.cons.car == sym) {
            pair->u.cons.cdr = val;
            return val;
        }
    }
    set_err(L, "lisp: set! unbound symbol");
    return L->nil_val;
}

static val_t *env_define(bm_lisp_t *L, val_t *env, val_t *sym, val_t *val)
{
    for (val_t *e = env; e && e->tag == V_CONS; e = e->u.cons.cdr) {
        val_t *pair = e->u.cons.car;
        if (pair && pair->tag == V_CONS && pair->u.cons.car == sym) {
            pair->u.cons.cdr = val;
            return env;
        }
    }
    return cons(L, cons(L, sym, val), env);
}

/* ---------------------------------------------------------------- Reader */

typedef struct {
    const char *p;
} reader_t;

static void skip_ws(reader_t *r)
{
    while (*r->p) {
        if (isspace((unsigned char)*r->p)) {
            r->p++;
        } else if (*r->p == ';') {
            while (*r->p && *r->p != '\n') r->p++;
        } else {
            break;
        }
    }
}

static int is_sym_char(char c)
{
    if (!c || isspace((unsigned char)c)) return 0;
    return strchr("()'\";`,", c) == NULL;
}

static val_t *read_expr(bm_lisp_t *L, reader_t *r, int depth);

static val_t *read_list(bm_lisp_t *L, reader_t *r, int depth)
{
    skip_ws(r);
    if (*r->p == ')') { r->p++; return L->nil_val; }
    if (!*r->p) { set_err(L, "lisp: unexpected EOF in list"); return L->nil_val; }

    val_t *head = L->nil_val, *tail = L->nil_val;
    while (*r->p) {
        skip_ws(r);
        if (*r->p == ')') { r->p++; return head; }
        if (!*r->p) { set_err(L, "lisp: unclosed '('"); return L->nil_val; }

        /* dotted pair */
        if (r->p[0] == '.' && !is_sym_char(r->p[1])) {
            r->p++;
            val_t *last = read_expr(L, r, depth + 1);
            if (L->has_err) return L->nil_val;
            skip_ws(r);
            if (*r->p != ')') { set_err(L, "lisp: expected ')' after dot"); return L->nil_val; }
            r->p++;
            if (tail == L->nil_val) return last;
            tail->u.cons.cdr = last;
            return head;
        }

        val_t *item = read_expr(L, r, depth + 1);
        if (L->has_err) return L->nil_val;
        val_t *node = cons(L, item, L->nil_val);
        if (head == L->nil_val) head = tail = node;
        else { tail->u.cons.cdr = node; tail = node; }
    }
    set_err(L, "lisp: unclosed '('");
    return L->nil_val;
}

static val_t *read_expr(bm_lisp_t *L, reader_t *r, int depth)
{
    if (L->has_err) return L->nil_val;
    if (depth > LISP_MAX_DEPTH) { set_err(L, "lisp: reader depth exceeded"); return L->nil_val; }
    skip_ws(r);
    char c = *r->p;
    if (!c) return NULL;

    if (c == '(') { r->p++; return read_list(L, r, depth + 1); }
    if (c == ')') { set_err(L, "lisp: unexpected ')'"); return L->nil_val; }
    if (c == '\'') {
        r->p++;
        val_t *x = read_expr(L, r, depth + 1);
        return cons(L, L->s_quote, cons(L, x, L->nil_val));
    }
    if (c == '`') {
        r->p++;
        val_t *x = read_expr(L, r, depth + 1);
        return cons(L, L->s_quasiquote, cons(L, x, L->nil_val));
    }
    if (c == ',') {
        r->p++;
        int splice = (*r->p == '@');
        if (splice) r->p++;
        val_t *x = read_expr(L, r, depth + 1);
        return cons(L, splice ? L->s_unquote_splicing : L->s_unquote, cons(L, x, L->nil_val));
    }
    if (c == '"') {
        r->p++;
        char buf[1024];
        uint32_t n = 0;
        while (*r->p && *r->p != '"') {
            char ch = *r->p++;
            if (ch == '\\' && *r->p) {
                char esc = *r->p++;
                if (esc == 'n') ch = '\n';
                else if (esc == 't') ch = '\t';
                else if (esc == 'r') ch = '\r';
                else ch = esc;
            }
            if (n + 1 < sizeof(buf)) buf[n++] = ch;
        }
        if (*r->p != '"') { set_err(L, "lisp: unclosed string"); return L->nil_val; }
        r->p++;
        return mk_str_n(L, buf, n);
    }

    const char *start = r->p;
    while (is_sym_char(*r->p)) r->p++;
    uint32_t len = (uint32_t)(r->p - start);
    if (len == 0) { set_err(L, "lisp: unexpected character"); return L->nil_val; }

    /* #t / #f / nil */
    if (len == 2 && start[0] == '#' && start[1] == 't') return L->true_val;
    if (len == 2 && start[0] == '#' && start[1] == 'f') return L->false_val;
    if (len == 3 && memcmp(start, "nil", 3) == 0) return L->nil_val;

    /* integer check */
    uint32_t i = 0;
    int neg = 0;
    if ((start[0] == '-' || start[0] == '+') && len > 1) {
        neg = (start[0] == '-');
        i = 1;
    }
    int is_num = 1;
    for (; i < len; i++) {
        if (!isdigit((unsigned char)start[i])) { is_num = 0; break; }
    }
    if (is_num) {
        int32_t v = 0;
        uint32_t k = (start[0] == '-' || start[0] == '+') ? 1 : 0;
        for (; k < len; k++) v = v * 10 + (start[k] - '0');
        return mk_int(L, neg ? -v : v);
    }
    return intern_n(L, start, len);
}

/* ---------------------------------------------------------------- Printer */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} strbuf_t;

static void sb_putc(strbuf_t *sb, char c)
{
    if (sb->len + 1 < sb->cap) sb->buf[sb->len] = c;
    sb->len++;
}

static void sb_puts(strbuf_t *sb, const char *s)
{
    while (*s) sb_putc(sb, *s++);
}

static void print_val(val_t *v, strbuf_t *sb, int readable)
{
    if (!v || v->tag == V_NIL) { sb_puts(sb, "()"); return; }
    char tmp[64];
    switch (v->tag) {
    case V_NIL: sb_puts(sb, "()"); break;
    case V_BOOL: sb_puts(sb, v->u.i ? "#t" : "#f"); break;
    case V_INT:
        snprintf(tmp, sizeof(tmp), "%d", (int)v->u.i);
        sb_puts(sb, tmp);
        break;
    case V_SYM: sb_puts(sb, v->u.sym.s); break;
    case V_STR:
        if (!readable) {
            for (uint32_t i = 0; i < v->u.str.len; i++) sb_putc(sb, v->u.str.s[i]);
        } else {
            sb_putc(sb, '"');
            for (uint32_t i = 0; i < v->u.str.len; i++) {
                char c = v->u.str.s[i];
                if (c == '"' || c == '\\') { sb_putc(sb, '\\'); sb_putc(sb, c); }
                else if (c == '\n') sb_puts(sb, "\\n");
                else sb_putc(sb, c);
            }
            sb_putc(sb, '"');
        }
        break;
    case V_PRIM: sb_puts(sb, "#<prim>"); break;
    case V_CLOSURE: sb_puts(sb, "#<lambda>"); break;
    case V_MACRO: sb_puts(sb, "#<macro>"); break;
    case V_CONS:
        sb_putc(sb, '(');
        for (val_t *p = v;;) {
            print_val(p->u.cons.car, sb, readable);
            p = p->u.cons.cdr;
            if (!p || p->tag == V_NIL) break;
            if (p->tag != V_CONS) {
                sb_puts(sb, " . ");
                print_val(p, sb, readable);
                break;
            }
            sb_putc(sb, ' ');
        }
        sb_putc(sb, ')');
        break;
    }
}

/* ---------------------------------------------------------------- Evaluator */

static val_t *eval(bm_lisp_t *L, val_t *expr, val_t *env);
static val_t *apply_fn(bm_lisp_t *L, val_t *fn, val_t *args);

static val_t *eval_list(bm_lisp_t *L, val_t *list, val_t *env)
{
    val_t *head = L->nil_val, *tail = L->nil_val;
    for (val_t *p = list; p && p->tag == V_CONS; p = p->u.cons.cdr) {
        val_t *v = eval(L, p->u.cons.car, env);
        if (L->has_err) return L->nil_val;
        val_t *node = cons(L, v, L->nil_val);
        if (head == L->nil_val) head = tail = node;
        else { tail->u.cons.cdr = node; tail = node; }
    }
    return head;
}

static val_t *eval_seq(bm_lisp_t *L, val_t *seq, val_t *env)
{
    val_t *res = L->nil_val;
    for (val_t *p = seq; p && p->tag == V_CONS; p = p->u.cons.cdr) {
        res = eval(L, p->u.cons.car, env);
        if (L->has_err) return L->nil_val;
    }
    return res;
}

static val_t *bind_params(bm_lisp_t *L, val_t *params, val_t *args, val_t *env)
{
    while (params && params->tag == V_CONS) {
        if (!args || args->tag != V_CONS) {
            set_err(L, "lisp: too few arguments");
            return env;
        }
        env = cons(L, cons(L, params->u.cons.car, args->u.cons.car), env);
        params = params->u.cons.cdr;
        args = args->u.cons.cdr;
    }
    if (params && params->tag == V_SYM) {
        env = cons(L, cons(L, params, args ? args : L->nil_val), env);
    } else if (args && args->tag == V_CONS) {
        set_err(L, "lisp: too many arguments");
    }
    return env;
}

static int equal_vals(val_t *a, val_t *b)
{
    if (a == b) return 1;
    if (!a || !b || a->tag != b->tag) return 0;
    switch (a->tag) {
    case V_NIL: return 1;
    case V_BOOL:
    case V_INT: return a->u.i == b->u.i;
    case V_SYM: return a == b;
    case V_STR: return a->u.str.len == b->u.str.len &&
                       memcmp(a->u.str.s, b->u.str.s, a->u.str.len) == 0;
    case V_CONS: return equal_vals(a->u.cons.car, b->u.cons.car) &&
                        equal_vals(a->u.cons.cdr, b->u.cons.cdr);
    default: return 0;
    }
}

/* Structural pattern matching: ?var binds, _ is wildcard, literals match equal */
static int match_pat(bm_lisp_t *L, val_t *pat, val_t *val, val_t **env)
{
    if (!pat || pat->tag == V_NIL) return (!val || val->tag == V_NIL);
    if (pat == L->s_wild) return 1;
    if (pat->tag == V_SYM) {
        if (pat->u.sym.s[0] == '?') {
            *env = cons(L, cons(L, pat, val), *env);
            return 1;
        }
        return val == pat;
    }
    if (pat->tag == V_INT || pat->tag == V_STR || pat->tag == V_BOOL)
        return equal_vals(pat, val);
    if (pat->tag == V_CONS) {
        if (pat->u.cons.car == L->s_quote && pat->u.cons.cdr && pat->u.cons.cdr->tag == V_CONS)
            return equal_vals(pat->u.cons.cdr->u.cons.car, val);
        if (!val || val->tag != V_CONS) return 0;
        return match_pat(L, pat->u.cons.car, val->u.cons.car, env) &&
               match_pat(L, pat->u.cons.cdr, val->u.cons.cdr, env);
    }
    return 0;
}

static val_t *append2(bm_lisp_t *L, val_t *a, val_t *b)
{
    if (!a || a->tag == V_NIL) return b;
    if (a->tag != V_CONS) return b;
    return cons(L, a->u.cons.car, append2(L, a->u.cons.cdr, b));
}

static val_t *eval_qq(bm_lisp_t *L, val_t *expr, val_t *env, int depth)
{
    if (!expr || expr->tag != V_CONS) return expr;
    if (expr->u.cons.car == L->s_unquote && depth == 1)
        return eval(L, car(expr->u.cons.cdr), env);
    if (expr->u.cons.car == L->s_quasiquote) {
        return cons(L, L->s_quasiquote,
                    cons(L, eval_qq(L, car(expr->u.cons.cdr), env, depth + 1), L->nil_val));
    }
    val_t *head = L->nil_val, *tail = L->nil_val;
    for (val_t *p = expr; p && p->tag == V_CONS; p = p->u.cons.cdr) {
        val_t *el = p->u.cons.car;
        if (el && el->tag == V_CONS && el->u.cons.car == L->s_unquote_splicing && depth == 1) {
            val_t *sp = eval(L, car(el->u.cons.cdr), env);
            if (L->has_err) return L->nil_val;
            for (val_t *q = sp; q && q->tag == V_CONS; q = q->u.cons.cdr) {
                val_t *node = cons(L, q->u.cons.car, L->nil_val);
                if (head == L->nil_val) head = tail = node;
                else { tail->u.cons.cdr = node; tail = node; }
            }
        } else {
            val_t *ev = eval_qq(L, el, env, depth);
            if (L->has_err) return L->nil_val;
            val_t *node = cons(L, ev, L->nil_val);
            if (head == L->nil_val) head = tail = node;
            else { tail->u.cons.cdr = node; tail = node; }
        }
        if (p->u.cons.cdr && p->u.cons.cdr->tag != V_CONS) {
            val_t *rest = eval_qq(L, p->u.cons.cdr, env, depth);
            if (head == L->nil_val) return rest;
            tail->u.cons.cdr = rest;
            break;
        }
    }
    return head;
}

static val_t *apply_fn(bm_lisp_t *L, val_t *fn, val_t *args)
{
    if (L->has_err) return L->nil_val;
    if (fn->tag == V_PRIM) return fn->u.prim.fn(L, args);
    if (fn->tag == V_CLOSURE) {
        val_t *nenv = bind_params(L, fn->u.clos.params, args, fn->u.clos.env);
        if (L->has_err) return L->nil_val;
        return eval_seq(L, fn->u.clos.body, nenv);
    }
    set_err(L, "lisp: attempt to call non-function");
    return L->nil_val;
}

static val_t *eval(bm_lisp_t *L, val_t *expr, val_t *env)
{
    if (L->has_err) return L->nil_val;
    if (++L->steps > LISP_MAX_STEPS) { set_err(L, "lisp: step limit exceeded"); return L->nil_val; }
    if (L->depth >= LISP_MAX_DEPTH) { set_err(L, "lisp: eval depth exceeded"); return L->nil_val; }
    L->depth++;

    val_t *res = L->nil_val;
    if (!expr || expr->tag == V_NIL || expr->tag == V_INT || expr->tag == V_STR ||
        expr->tag == V_BOOL || expr->tag == V_PRIM || expr->tag == V_CLOSURE) {
        res = expr ? expr : L->nil_val;
        L->depth--;
        return res;
    }
    if (expr->tag == V_SYM) {
        res = env_lookup(L, env, expr);
        L->depth--;
        return res;
    }

    val_t *op = expr->u.cons.car;
    val_t *args = expr->u.cons.cdr;

    if (op == L->s_quote) {
        res = car(args);
    } else if (op == L->s_quasiquote) {
        res = eval_qq(L, car(args), env, 1);
    } else if (op == L->s_if) {
        val_t *c = eval(L, car(args), env);
        if (!L->has_err) {
            if (is_truthy(c)) res = eval(L, car(cdr(args)), env);
            else if (cdr(cdr(args)) && cdr(cdr(args))->tag == V_CONS)
                res = eval(L, car(cdr(cdr(args))), env);
        }
    } else if (op == L->s_cond) {
        for (val_t *cl = args; cl && cl->tag == V_CONS; cl = cl->u.cons.cdr) {
            val_t *clause = cl->u.cons.car;
            val_t *test = car(clause);
            if (test == L->s_else || is_truthy(eval(L, test, env))) {
                if (!L->has_err) res = eval_seq(L, cdr(clause), env);
                break;
            }
            if (L->has_err) break;
        }
    } else if (op == L->s_and) {
        res = L->true_val;
        for (val_t *p = args; p && p->tag == V_CONS; p = p->u.cons.cdr) {
            res = eval(L, p->u.cons.car, env);
            if (L->has_err || !is_truthy(res)) break;
        }
    } else if (op == L->s_or) {
        res = L->false_val;
        for (val_t *p = args; p && p->tag == V_CONS; p = p->u.cons.cdr) {
            res = eval(L, p->u.cons.car, env);
            if (L->has_err || is_truthy(res)) break;
        }
    } else if (op == L->s_begin || op == L->s_do) {
        res = eval_seq(L, args, env);
    } else if (op == L->s_lambda) {
        res = mk_val(L, V_CLOSURE);
        res->u.clos.params = car(args);
        res->u.clos.body = cdr(args);
        res->u.clos.env = env;
    } else if (op == L->s_define || op == L->s_defmacro) {
        int is_mac = (op == L->s_defmacro);
        val_t *target = car(args);
        if (target && target->tag == V_CONS) {
            val_t *name = target->u.cons.car;
            val_t *fn = mk_val(L, is_mac ? V_MACRO : V_CLOSURE);
            fn->u.clos.params = target->u.cons.cdr;
            fn->u.clos.body = cdr(args);
            fn->u.clos.env = env;
            L->global_env = env_define(L, L->global_env, name, fn);
            /* refresh closure env so recursion works cleanly */
            fn->u.clos.env = L->global_env;
            res = name;
        } else if (target && target->tag == V_SYM) {
            val_t *v = eval(L, car(cdr(args)), env);
            if (!L->has_err) {
                if (is_mac && v->tag == V_CLOSURE) v->tag = V_MACRO;
                L->global_env = env_define(L, L->global_env, target, v);
                if (v->tag == V_CLOSURE || v->tag == V_MACRO)
                    v->u.clos.env = L->global_env;
                res = target;
            }
        }
    } else if (op == L->s_set) {
        val_t *v = eval(L, car(cdr(args)), env);
        if (!L->has_err) res = env_set(L, env, car(args), v);
    } else if (op == L->s_let || op == L->s_lets) {
        val_t *bindings = car(args);
        val_t *nenv = env;
        int seq_b = (op == L->s_lets);
        for (val_t *b = bindings; b && b->tag == V_CONS; b = b->u.cons.cdr) {
            val_t *pair = b->u.cons.car;
            val_t *v = eval(L, car(cdr(pair)), seq_b ? nenv : env);
            if (L->has_err) break;
            nenv = cons(L, cons(L, car(pair), v), nenv);
        }
        if (!L->has_err) res = eval_seq(L, cdr(args), nenv);
    } else if (op == L->s_match) {
        val_t *target = eval(L, car(args), env);
        if (!L->has_err) {
            for (val_t *cl = cdr(args); cl && cl->tag == V_CONS; cl = cl->u.cons.cdr) {
                val_t *clause = cl->u.cons.car;
                val_t *nenv = env;
                if (match_pat(L, car(clause), target, &nenv)) {
                    res = eval_seq(L, cdr(clause), nenv);
                    break;
                }
            }
        }
    } else {
        val_t *fn = eval(L, op, env);
        if (!L->has_err) {
            if (fn->tag == V_MACRO) {
                val_t *nenv = bind_params(L, fn->u.clos.params, args, fn->u.clos.env);
                val_t *expanded = L->has_err ? L->nil_val : eval_seq(L, fn->u.clos.body, nenv);
                if (!L->has_err) res = eval(L, expanded, env);
            } else {
                val_t *ev_args = eval_list(L, args, env);
                if (!L->has_err) res = apply_fn(L, fn, ev_args);
            }
        }
    }

    L->depth--;
    return res;
}

/* ---------------------------------------------------------------- Builtins */

static val_t *p_car(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return (x && x->tag == V_CONS) ? x->u.cons.car : L->nil_val; }
static val_t *p_cdr(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return (x && x->tag == V_CONS) ? x->u.cons.cdr : L->nil_val; }
static val_t *p_cons(bm_lisp_t *L, val_t *a) { return cons(L, car(a), car(cdr(a))); }
static val_t *p_list(bm_lisp_t *L, val_t *a) { (void)L; return a; }
static val_t *p_null(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return mk_bool(L, !x || x->tag == V_NIL); }
static val_t *p_pair(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return mk_bool(L, x && x->tag == V_CONS); }
static val_t *p_sym(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return mk_bool(L, x && x->tag == V_SYM); }
static val_t *p_num(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return mk_bool(L, x && x->tag == V_INT); }
static val_t *p_str(bm_lisp_t *L, val_t *a) { val_t *x = car(a); return mk_bool(L, x && x->tag == V_STR); }
static val_t *p_not(bm_lisp_t *L, val_t *a) { return mk_bool(L, !is_truthy(car(a))); }
static val_t *p_eq(bm_lisp_t *L, val_t *a) { return mk_bool(L, car(a) == car(cdr(a))); }
static val_t *p_equal(bm_lisp_t *L, val_t *a) { return mk_bool(L, equal_vals(car(a), car(cdr(a)))); }

static val_t *p_append(bm_lisp_t *L, val_t *a)
{
    if (!a || a->tag == V_NIL) return L->nil_val;
    if (!a->u.cons.cdr || a->u.cons.cdr->tag == V_NIL) return a->u.cons.car;
    return append2(L, a->u.cons.car, p_append(L, a->u.cons.cdr));
}

static val_t *p_length(bm_lisp_t *L, val_t *a)
{
    int32_t n = 0;
    for (val_t *p = car(a); p && p->tag == V_CONS; p = p->u.cons.cdr) n++;
    return mk_int(L, n);
}

static val_t *p_nth(bm_lisp_t *L, val_t *a)
{
    val_t *lst = car(a);
    val_t *idx = car(cdr(a));
    int32_t n = (idx && idx->tag == V_INT) ? idx->u.i : 0;
    while (lst && lst->tag == V_CONS && n > 0) { lst = lst->u.cons.cdr; n--; }
    return (lst && lst->tag == V_CONS) ? lst->u.cons.car : L->nil_val;
}

static val_t *p_assoc(bm_lisp_t *L, val_t *a)
{
    val_t *k = car(a);
    for (val_t *p = car(cdr(a)); p && p->tag == V_CONS; p = p->u.cons.cdr) {
        val_t *pair = p->u.cons.car;
        if (pair && pair->tag == V_CONS && equal_vals(pair->u.cons.car, k))
            return pair;
    }
    return L->false_val;
}

static val_t *p_add(bm_lisp_t *L, val_t *a)
{
    int32_t s = 0;
    for (val_t *p = a; p && p->tag == V_CONS; p = p->u.cons.cdr)
        if (p->u.cons.car->tag == V_INT) s += p->u.cons.car->u.i;
    return mk_int(L, s);
}

static val_t *p_sub(bm_lisp_t *L, val_t *a)
{
    if (!a || a->tag != V_CONS) return mk_int(L, 0);
    int32_t s = (a->u.cons.car->tag == V_INT) ? a->u.cons.car->u.i : 0;
    if (a->u.cons.cdr->tag == V_NIL) return mk_int(L, -s);
    for (val_t *p = a->u.cons.cdr; p && p->tag == V_CONS; p = p->u.cons.cdr)
        if (p->u.cons.car->tag == V_INT) s -= p->u.cons.car->u.i;
    return mk_int(L, s);
}

static val_t *p_mul(bm_lisp_t *L, val_t *a)
{
    int32_t s = 1;
    for (val_t *p = a; p && p->tag == V_CONS; p = p->u.cons.cdr)
        if (p->u.cons.car->tag == V_INT) s *= p->u.cons.car->u.i;
    return mk_int(L, s);
}

static val_t *p_div(bm_lisp_t *L, val_t *a)
{
    val_t *x = car(a), *y = car(cdr(a));
    if (!x || x->tag != V_INT || !y || y->tag != V_INT || y->u.i == 0) {
        set_err(L, "lisp: division by zero");
        return mk_int(L, 0);
    }
    return mk_int(L, x->u.i / y->u.i);
}

static val_t *p_mod(bm_lisp_t *L, val_t *a)
{
    val_t *x = car(a), *y = car(cdr(a));
    if (!x || x->tag != V_INT || !y || y->tag != V_INT || y->u.i == 0)
        return mk_int(L, 0);
    return mk_int(L, x->u.i % y->u.i);
}

static val_t *p_cmp(bm_lisp_t *L, val_t *a, int mode)
{
    val_t *x = car(a), *y = car(cdr(a));
    if (!x || !y || x->tag != V_INT || y->tag != V_INT) return L->false_val;
    int32_t xi = x->u.i, yi = y->u.i;
    int r = (mode == 0) ? (xi == yi) : (mode == 1) ? (xi < yi) : (mode == 2) ? (xi > yi) : (xi <= yi);
    return mk_bool(L, r);
}
static val_t *p_num_eq(bm_lisp_t *L, val_t *a) { return p_cmp(L, a, 0); }
static val_t *p_lt(bm_lisp_t *L, val_t *a) { return p_cmp(L, a, 1); }
static val_t *p_gt(bm_lisp_t *L, val_t *a) { return p_cmp(L, a, 2); }
static val_t *p_le(bm_lisp_t *L, val_t *a) { return p_cmp(L, a, 3); }

static val_t *p_str_cat(bm_lisp_t *L, val_t *a)
{
    char buf[8192];
    strbuf_t sb = { buf, sizeof(buf), 0 };
    for (val_t *p = a; p && p->tag == V_CONS; p = p->u.cons.cdr)
        print_val(p->u.cons.car, &sb, 0);
    uint32_t len = (sb.len < sizeof(buf)) ? (uint32_t)sb.len : (uint32_t)(sizeof(buf) - 1);
    return mk_str_n(L, buf, len);
}

static val_t *p_sym_to_str(bm_lisp_t *L, val_t *a)
{
    val_t *x = car(a);
    if (!x || x->tag != V_SYM) return mk_str_n(L, "", 0);
    return mk_str_n(L, x->u.sym.s, x->u.sym.len);
}

static val_t *p_str_to_sym(bm_lisp_t *L, val_t *a)
{
    val_t *x = car(a);
    if (!x || x->tag != V_STR) return L->nil_val;
    return intern_n(L, x->u.str.s, x->u.str.len);
}

static val_t *p_apply(bm_lisp_t *L, val_t *a)
{
    return apply_fn(L, car(a), car(cdr(a)));
}

static val_t *p_map(bm_lisp_t *L, val_t *a)
{
    val_t *fn = car(a), *lst = car(cdr(a));
    val_t *head = L->nil_val, *tail = L->nil_val;
    for (val_t *p = lst; p && p->tag == V_CONS; p = p->u.cons.cdr) {
        val_t *v = apply_fn(L, fn, cons(L, p->u.cons.car, L->nil_val));
        if (L->has_err) return L->nil_val;
        val_t *node = cons(L, v, L->nil_val);
        if (head == L->nil_val) head = tail = node;
        else { tail->u.cons.cdr = node; tail = node; }
    }
    return head;
}

static val_t *p_filter(bm_lisp_t *L, val_t *a)
{
    val_t *fn = car(a), *lst = car(cdr(a));
    val_t *head = L->nil_val, *tail = L->nil_val;
    for (val_t *p = lst; p && p->tag == V_CONS; p = p->u.cons.cdr) {
        val_t *v = apply_fn(L, fn, cons(L, p->u.cons.car, L->nil_val));
        if (L->has_err) return L->nil_val;
        if (is_truthy(v)) {
            val_t *node = cons(L, p->u.cons.car, L->nil_val);
            if (head == L->nil_val) head = tail = node;
            else { tail->u.cons.cdr = node; tail = node; }
        }
    }
    return head;
}

static void add_prim(bm_lisp_t *L, const char *name, prim_fn_t fn)
{
    val_t *v = mk_val(L, V_PRIM);
    v->u.prim.name = name;
    v->u.prim.fn = fn;
    L->global_env = env_define(L, L->global_env, intern(L, name), v);
}

/* ---------------------------------------------------------------- Public API */

bm_lisp_t *bm_lisp_init(void *arena, size_t arena_bytes)
{
    if (!arena || arena_bytes < sizeof(bm_lisp_t) + 8192) return NULL;
    bm_lisp_t *L = (bm_lisp_t *)arena;
    memset(L, 0, sizeof(*L));
    L->base = (uint8_t *)arena;
    L->cap = arena_bytes;
    L->used = (sizeof(*L) + 7u) & ~(size_t)7u;

    L->nil_val = mk_val(L, V_NIL);
    L->true_val = mk_val(L, V_BOOL); L->true_val->u.i = 1;
    L->false_val = mk_val(L, V_BOOL); L->false_val->u.i = 0;
    L->global_env = L->nil_val;

    L->s_quote = intern(L, "quote");
    L->s_if = intern(L, "if");
    L->s_cond = intern(L, "cond");
    L->s_else = intern(L, "else");
    L->s_define = intern(L, "define");
    L->s_set = intern(L, "set!");
    L->s_lambda = intern(L, "lambda");
    L->s_let = intern(L, "let");
    L->s_lets = intern(L, "let*");
    L->s_begin = intern(L, "begin");
    L->s_do = intern(L, "do");
    L->s_and = intern(L, "and");
    L->s_or = intern(L, "or");
    L->s_defmacro = intern(L, "defmacro");
    L->s_match = intern(L, "match");
    L->s_quasiquote = intern(L, "quasiquote");
    L->s_unquote = intern(L, "unquote");
    L->s_unquote_splicing = intern(L, "unquote-splicing");
    L->s_wild = intern(L, "_");

    add_prim(L, "car", p_car); add_prim(L, "cdr", p_cdr);
    add_prim(L, "cons", p_cons); add_prim(L, "list", p_list);
    add_prim(L, "append", p_append); add_prim(L, "length", p_length);
    add_prim(L, "nth", p_nth); add_prim(L, "assoc", p_assoc);
    add_prim(L, "null?", p_null); add_prim(L, "pair?", p_pair);
    add_prim(L, "symbol?", p_sym); add_prim(L, "number?", p_num);
    add_prim(L, "string?", p_str); add_prim(L, "not", p_not);
    add_prim(L, "eq?", p_eq); add_prim(L, "equal?", p_equal);
    add_prim(L, "+", p_add); add_prim(L, "-", p_sub);
    add_prim(L, "*", p_mul); add_prim(L, "/", p_div);
    add_prim(L, "mod", p_mod); add_prim(L, "=", p_num_eq);
    add_prim(L, "<", p_lt); add_prim(L, ">", p_gt); add_prim(L, "<=", p_le);
    add_prim(L, "str-cat", p_str_cat);
    add_prim(L, "sym->str", p_sym_to_str);
    add_prim(L, "str->sym", p_str_to_sym);
    add_prim(L, "apply", p_apply);
    add_prim(L, "map", p_map);
    add_prim(L, "filter", p_filter);

    bm_lisp_freeze_base(L);
    return L;
}

void bm_lisp_freeze_base(bm_lisp_t *L)
{
    if (!L) return;
    L->base_used = L->used;
    L->base_env = L->global_env;
    memcpy(L->base_buckets, L->sym_buckets, sizeof(L->sym_buckets));
}

void bm_lisp_reset(bm_lisp_t *L)
{
    if (!L) return;
    L->used = L->base_used;
    L->global_env = L->base_env;
    memcpy(L->sym_buckets, L->base_buckets, sizeof(L->sym_buckets));
    L->has_err = 0;
    L->err[0] = '\0';
}

size_t bm_lisp_used(const bm_lisp_t *L)
{
    return L ? L->used : 0;
}

int bm_lisp_eval(bm_lisp_t *L, const char *src, char *out, size_t out_cap,
                 char *err, size_t err_cap)
{
    if (!L || !src) return -1;
    L->steps = 0;
    L->depth = 0;
    L->has_err = 0;
    L->err[0] = '\0';

    reader_t r = { src };
    val_t *last = L->nil_val;
    for (;;) {
        skip_ws(&r);
        if (!*r.p) break;
        val_t *expr = read_expr(L, &r, 0);
        if (L->has_err) break;
        if (!expr) break;
        last = eval(L, expr, L->global_env);
        if (L->has_err) break;
    }

    if (L->has_err) {
        if (err && err_cap) snprintf(err, err_cap, "%s", L->err);
        return -1;
    }
    if (out && out_cap) {
        strbuf_t sb = { out, out_cap, 0 };
        print_val(last, &sb, (last->tag != V_STR));
        size_t end = sb.len < out_cap ? sb.len : out_cap - 1;
        out[end] = '\0';
    }
    return 0;
}
