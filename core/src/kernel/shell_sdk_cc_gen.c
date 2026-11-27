/*
 * Shell SDK, bm C: the names, the types and the 6502 (see shell_sdk_cc_int.h). Original code.
 *
 * A value of a byte is in A, a value of 16 bits in A (low) and X (high). The other side of an operation waits on
 * the stack of the machine, then in __b. Every variable has a fixed place (no recursion): the zero page first.
 */
#include "shell_sdk_anim.h"
#include "shell_sdk_cc_int.h"
#include "shell_sdk_music.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { B_WAIT, B_HELD, B_PRESSED, B_BACKGROUND, B_BG_PAL, B_SPR_PAL, B_SPRITE, B_STYLE, B_HIDE, B_RANDOM, B_PEEK,
       B_POKE, B_BIG, B_PALETTE, B_RGB, B_SONG, B_SFX, B_STOP, B_COUNT };

enum { ON_NES = 1, ON_SNES = 2, ON_BOTH = 3 };

/* The library: the bytes of each argument on the NES and on the SNES (a colour and the buttons of the SNES take
 * 16 bits). The routines have the same names in the two runtimes. */
static const struct {
    const char *name, *args, *routine;
    int ret, nargs, nes[4], snes[4], on, part;
} builtins[B_COUNT] = {
    { "wait_frame", "()", "__wait_frame", CT_VOID, 0, { 0 }, { 0 }, ON_BOTH, 0 },
    { "held", "(button)", NULL, CT_U8, 1, { 1 }, { 2 }, ON_BOTH, 0 },
    { "pressed", "(button)", NULL, CT_U8, 1, { 1 }, { 2 }, ON_BOTH, 0 },
    { "background", "(colour)", "__background", CT_VOID, 1, { 1 }, { 2 }, ON_BOTH, RT_PALETTE },
    { "background_palette", "(n, c1, c2, c3)", "__background_palette", CT_VOID, 4, { 1, 1, 1, 1 }, { 0 }, ON_NES,
      RT_PALETTE },
    { "sprite_palette", "(n, c1, c2, c3)", "__sprite_palette", CT_VOID, 4, { 1, 1, 1, 1 }, { 1, 2, 2, 2 }, ON_BOTH,
      RT_PALETTE },
    { "sprite", "(n, x, y, tile)", "__sprite", CT_VOID, 4, { 1, 1, 1, 1 }, { 1, 2, 1, 2 }, ON_BOTH, RT_SPRITE },
    { "sprite_style", "(n, style)", "__sprite_style", CT_VOID, 2, { 1, 1 }, { 1, 1 }, ON_BOTH, RT_SPRITE },
    { "hide_sprite", "(n)", "__hide_sprite", CT_VOID, 1, { 1 }, { 1 }, ON_BOTH, RT_SPRITE },
    { "random", "()", "__random", CT_U8, 0, { 0 }, { 0 }, ON_BOTH, RT_RANDOM },
    { "peek", "(address)", NULL, CT_U8, 1, { 2 }, { 2 }, ON_BOTH, 0 },
    { "poke", "(address, value)", NULL, CT_VOID, 2, { 2, 1 }, { 2, 1 }, ON_BOTH, 0 },
    { "sprite_big", "(n, big)", "__sprite_big", CT_VOID, 2, { 0 }, { 1, 1 }, ON_SNES, RT_SPRITE },
    { "palette", "(index, colour)", "__palette", CT_VOID, 2, { 0 }, { 1, 2 }, ON_SNES, RT_PALETTE },
    { "rgb", "(r, g, b)", NULL, CT_U16, 3, { 0 }, { 1, 1, 1 }, ON_SNES, 0 },
    /* the sound driver (shell_sdk_snd.c) takes the number in A */
    { "play_song", "(song)", "play_song", CT_VOID, 1, { 1 }, { 0 }, ON_NES, 0 },
    { "play_sfx", "(effect)", "play_sfx", CT_VOID, 1, { 1 }, { 0 }, ON_NES, 0 },
    { "stop_music", "()", "stop_music", CT_VOID, 0, { 0 }, { 0 }, ON_NES, 0 } };

typedef struct { const char *name; int value; } constant_t;

static const constant_t nes_constants[] = {
    { "A", 0x80 }, { "B", 0x40 }, { "SELECT", 0x20 }, { "START", 0x10 }, { "UP", 0x08 }, { "DOWN", 0x04 },
    { "LEFT", 0x02 }, { "RIGHT", 0x01 },
    { "BLACK", 0x0F }, { "DARK_GRAY", 0x00 }, { "GRAY", 0x10 }, { "WHITE", 0x30 }, { "RED", 0x16 },
    { "ORANGE", 0x27 }, { "YELLOW", 0x28 }, { "GREEN", 0x1A }, { "LIME", 0x2A }, { "CYAN", 0x2C },
    { "SKY", 0x21 }, { "BLUE", 0x12 }, { "NAVY", 0x02 }, { "PURPLE", 0x14 }, { "PINK", 0x25 }, { "BROWN", 0x07 },
    { "FLIP_X", 0x40 }, { "FLIP_Y", 0x80 }, { "BEHIND", 0x20 } };

/* the buttons as the SNES reads them; the colours in BGR555 (red in the low bits) */
static const constant_t snes_constants[] = {
    { "B", 0x8000 }, { "Y", 0x4000 }, { "SELECT", 0x2000 }, { "START", 0x1000 }, { "UP", 0x0800 },
    { "DOWN", 0x0400 }, { "LEFT", 0x0200 }, { "RIGHT", 0x0100 }, { "A", 0x0080 }, { "X", 0x0040 }, { "L", 0x0020 },
    { "R", 0x0010 },
    { "BLACK", 0x0000 }, { "DARK_GRAY", 0x294A }, { "GRAY", 0x56B5 }, { "WHITE", 0x7FFF }, { "RED", 0x109F },
    { "ORANGE", 0x025F }, { "YELLOW", 0x13BF }, { "GREEN", 0x12C4 }, { "LIME", 0x1BEC }, { "CYAN", 0x7386 },
    { "SKY", 0x7ECC }, { "BLUE", 0x7144 }, { "NAVY", 0x3840 }, { "PURPLE", 0x60D2 }, { "PINK", 0x5A1F },
    { "BROWN", 0x0930 },
    { "FLIP_X", 0x40 }, { "FLIP_Y", 0x80 }, { "BEHIND", 0x20 } };

typedef struct {
    cc_t *cc;
    int snes;                       /* the machine */
    int ram_first, ram_end;
    cc_program_t prog;
    sym_t *globals;                 /* the names of the top */
    sym_t *locals;                  /* the names of the open blocks, the newest first */
    sym_t *all, **all_tail;         /* every symbol that needs a place or a line of the output */
    sym_t *fn;                      /* the function being read or written */
    int zp, ram;                    /* the next free place */
    int zp_end;
    char *sound;                    /* the driver and its data, when the program has sound */
    int loops;                      /* how many loops are open (analysis) */
    int labels, brk, cont;
    int parts;                      /* RT_* that the program uses */
    int line, commented;            /* the line of C of the output; the last one in a comment */
    char *text;
    size_t len, max;
    int *line_of;
    int lines, lines_max;
} gen_t;

/* ---- errors and memory ---- */

typedef struct cc_block { struct cc_block *next; } header_t;

void cc_error(cc_t *cc, int line, const char *fmt, ...)
{
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(cc->err, cc->en, "main.c:%d: %s", line > 0 ? line : 1, msg);
    longjmp(cc->fail, 1);
}

void *cc_alloc(cc_t *cc, size_t n)
{
    header_t *h = calloc(1, sizeof(header_t) + 8 + n);
    if (!h)
        cc_error(cc, 1, "not enough memory to compile");
    h->next = cc->blocks;
    cc->blocks = h;
    return (char *)h + ((sizeof(header_t) + 7) & ~(size_t)7);
}

char *cc_strdup(cc_t *cc, const char *s, size_t n)
{
    char *d = cc_alloc(cc, n + 1);
    memcpy(d, s, n);
    return d;
}

/* ---- types ---- */

static int width(ctype_t t) { return t.ptr || t.len || t.base == CT_U16 || t.base == CT_S16 ? 2 : 1; }
static int signed_type(ctype_t t) { return !t.ptr && !t.len && (t.base == CT_S8 || t.base == CT_S16); }
/* The bytes of what a pointer (or an array) holds. */
static int element(ctype_t t) { return t.ptr > (t.len ? 0 : 1) || t.base == CT_U16 || t.base == CT_S16 ? 2 : 1; }
static int is_signed(const node_t *n) { return n->constant ? n->value < 0 : signed_type(n->type); }

static ctype_t type_of_value(long v)
{
    ctype_t t = { CT_U8, 0, 0 };
    if (v < -128)
        t.base = CT_S16;
    else if (v < 0)
        t.base = CT_S8;
    else if (v > 255)
        t.base = CT_U16;
    return t;
}

static void make_constant(gen_t *g, node_t *n, long v)
{
    if (v < -32768 || v > 65535)
        cc_error(g->cc, n->line, "the value %ld does not fit in 16 bits", v);
    n->constant = 1;
    n->value = v;
    n->type = type_of_value(v);
}

static long fold(cc_t *cc, int line, int op, long a, long b)
{
    switch (op) {
    case OP_ADD: return a + b;
    case OP_SUB: return a - b;
    case OP_MUL: return a * b;
    case OP_DIV: case OP_MOD:
        if (!b)
            cc_error(cc, line, "division by zero");
        return op == OP_DIV ? a / b : a % b;
    case OP_AND: return a & b;
    case OP_OR: return a | b;
    case OP_XOR: return a ^ b;
    case OP_SHL: return b < 0 || b > 15 ? 0 : (a << b) & 0xFFFF;
    case OP_SHR: return b < 0 ? a : a >> (b > 31 ? 31 : b);
    case OP_EQ: return a == b;
    case OP_NE: return a != b;
    case OP_LT: return a < b;
    case OP_GT: return a > b;
    case OP_LE: return a <= b;
    case OP_GE: return a >= b;
    case OP_NEG: return -a;
    case OP_NOT: return !a;
    default: return ~a;
    }
}

/* ---- names ---- */

static sym_t *find_in(sym_t *list, const char *name)
{
    for (sym_t *s = list; s; s = s->next)
        if (!strcmp(s->name, name))
            return s;
    return NULL;
}

static sym_t *find(gen_t *g, const char *name)
{
    sym_t *s = find_in(g->locals, name);
    return s ? s : find_in(g->globals, name);
}

/* How many letters to change to go from a to b (at most 3 matter). */
static int distance(const char *a, const char *b)
{
    int row[48];
    const int n = (int)strlen(a), m = (int)strlen(b);
    if (n > 40 || m > 40 || n - m > 2 || m - n > 2)
        return 9;
    for (int j = 0; j <= m; j++)
        row[j] = j;
    for (int i = 1; i <= n; i++) {
        int corner = row[0];
        row[0] = i;
        for (int j = 1; j <= m; j++) {
            int v = corner + (a[i - 1] != b[j - 1]);
            if (row[j] + 1 < v)
                v = row[j] + 1;
            if (row[j - 1] + 1 < v)
                v = row[j - 1] + 1;
            corner = row[j];
            row[j] = v;
        }
    }
    return row[m];
}

static void unknown(gen_t *g, int line, const char *name)
{
    const char *best = NULL;
    int far = strlen(name) < 3 ? 2 : 3;
    sym_t *lists[2] = { g->locals, g->globals };
    for (int i = 0; i < 2; i++)
        for (sym_t *s = lists[i]; s; s = s->next) {
            const int d = distance(name, s->name);
            if (d < far)
                far = d, best = s->name;
        }
    if (best)
        cc_error(g->cc, line, "'%s' is not declared. Did you mean '%s'?", name, best);
    cc_error(g->cc, line, "'%s' is not declared: write its type first, as in byte %s;", name, name);
}

static sym_t *new_sym(gen_t *g, const char *name, int kind, int line)
{
    sym_t *s = cc_alloc(g->cc, sizeof *s);
    s->name = name;
    s->kind = (uint8_t)kind;
    s->line = line;
    return s;
}

static void keep(gen_t *g, sym_t *s)
{
    *g->all_tail = s;
    g->all_tail = &s->all;
}

static const char *what(const sym_t *s)
{
    return s->kind == SY_FUNC ? "a function" : s->kind == SY_BUILTIN ? "a function of the library" :
           s->kind == SY_CONST ? "a constant" : "a variable";
}

static void declare_global(gen_t *g, sym_t *s)
{
    const sym_t *old = find_in(g->globals, s->name);
    if (old) {
        if (old->line)
            cc_error(g->cc, s->line, "'%s' is already %s, declared at line %d", s->name, what(old), old->line);
        cc_error(g->cc, s->line, "'%s' is a name of bm C (%s): choose another", s->name, what(old));
    }
    s->next = g->globals;
    g->globals = s;
}

/* A label for a name: prefix, the function, the name, a number when it is taken already. */
static void make_label(gen_t *g, sym_t *s, const char *prefix, const char *fn)
{
    for (int n = 1;; n++) {
        char number[16] = "";
        if (n > 1)
            snprintf(number, sizeof number, "__%d", n);
        snprintf(s->label, sizeof s->label, "%s%s%s%s%s", prefix, fn ? fn : "", fn ? "_" : "", s->name, number);
        const sym_t *o = g->all;
        while (o && (o == s || strcmp(o->label, s->label)))
            o = o->all;
        if (!o)
            return;
    }
}

static int bytes_of(const sym_t *s)
{
    return s->type.len ? s->type.len * element(s->type) : width(s->type);
}

/* A place in the RAM: single values go to the zero page, which is faster. */
static void place(gen_t *g, sym_t *s)
{
    const int n = bytes_of(s);
    if (!s->type.len && g->zp + n <= g->zp_end) {
        s->address = g->zp;
        g->zp += n;
    } else if (g->ram + n <= g->ram_end) {
        s->address = g->ram;
        g->ram += n;
    } else
        cc_error(g->cc, s->line, "no room for '%s': it takes %d bytes, the RAM has %d left", s->name, n,
                 g->ram_end - g->ram);
}

/* ---- the constants ---- */

/* rgb(r, g, b): the colour of the SNES, 0 to 31 each, worked out when the program is built. */
static long colour_of(cc_t *cc, node_t *n)
{
    long v[3] = { 0, 0, 0 };
    int count = 0;
    for (node_t *a = n->a; a; a = a->next, count++)
        if (count < 3) {
            v[count] = cc_constant(cc, a);
            if (v[count] < 0 || v[count] > 31)
                cc_error(cc, a->line, "rgb takes 0 to 31 for red, green and blue, not %ld", v[count]);
        }
    if (count != 3)
        cc_error(cc, n->line, "rgb(r, g, b) takes 3 arguments, not %d", count);
    return v[0] | v[1] << 5 | v[2] << 10;
}

long cc_constant(cc_t *cc, node_t *n)
{
    gen_t *g = cc->user;
    switch (n->kind) {
    case N_NUM:
        return n->value;
    case N_NAME: {
        const sym_t *s = find(g, n->name);
        if (s && s->kind == SY_CONST)
            return s->value;
        if (!s)
            cc_error(cc, n->line, "'%s' is not a #define or a constant declared before", n->name);
        break;
    }
    case N_UNARY:
        return fold(cc, n->line, n->op, cc_constant(cc, n->a), 0);
    case N_BINARY:
        return fold(cc, n->line, n->op, cc_constant(cc, n->a), cc_constant(cc, n->b));
    case N_AND:
        return cc_constant(cc, n->a) && cc_constant(cc, n->b);
    case N_OR:
        return cc_constant(cc, n->a) || cc_constant(cc, n->b);
    case N_COND:
        return cc_constant(cc, n->a) ? cc_constant(cc, n->b) : cc_constant(cc, n->c);
    case N_CAST: {
        const long v = cc_constant(cc, n->a);
        return width(n->type) == 2 ? v & 0xFFFF : v & 0xFF;
    }
    case N_CALL:
        if (g->snes && !strcmp(n->name, "rgb"))
            return colour_of(cc, n);
        break;
    default:
        break;
    }
    cc_error(cc, n->line, "this needs a number that is known when the program is built");
}

static void define(cc_t *cc, const char *name, node_t *value, int line)
{
    gen_t *g = cc->user;
    sym_t *s = new_sym(g, name, SY_CONST, line);
    s->value = cc_constant(cc, value);
    declare_global(g, s);
}

/* ---- analysis: names and types of the expressions ---- */

static ctype_t expr(gen_t *g, node_t *n);

static ctype_t value_of(gen_t *g, node_t *n)
{
    const ctype_t t = expr(g, n);
    if (t.base == CT_VOID && !t.ptr)
        cc_error(g->cc, n->line, "'%s' gives no value", n->kind == N_CALL ? n->name : "this");
    return t;
}

static void fits(gen_t *g, const node_t *v, ctype_t to, const char *name)
{
    if (!v->constant || to.ptr)
        return;
    if (width(to) == 1 && (v->value < -128 || v->value > 255))
        cc_error(g->cc, v->line, "%ld does not fit in '%s': a byte holds 0 to 255 (use word)", v->value, name);
}

static int has_effect(const node_t *n)
{
    if (!n || n->constant)
        return 0;
    if (n->kind == N_CALL || n->kind == N_ASSIGN || n->kind == N_PREINC || n->kind == N_POSTINC)
        return 1;
    return has_effect(n->a) || has_effect(n->b) || has_effect(n->c);
}

/* n is a place that can be written. */
static void writable(gen_t *g, const node_t *n)
{
    if (n->kind == N_NAME && n->sym) {
        if (n->sym->type.len)
            cc_error(g->cc, n->line, "'%s' is an array: write one of its values, %s[0]", n->name, n->name);
        if (n->sym->kind == SY_VAR && !n->sym->is_const)
            return;
        cc_error(g->cc, n->line, "'%s' cannot change: it is %s", n->name,
                 n->sym->is_const ? "const" : what(n->sym));
    }
    if (n->kind == N_NAME)
        cc_error(g->cc, n->line, "'%s' cannot change: it is a constant", n->name);
    if (n->kind == N_INDEX) {
        if (n->a->kind == N_NAME && n->a->sym && n->a->sym->kind == SY_ROM)
            cc_error(g->cc, n->line, "'%s' is const: it is in the ROM and cannot change", n->a->name);
        return;
    }
    if (n->kind == N_DEREF)
        return;
    cc_error(g->cc, n->line, "a value can be put in a variable, an array element or *pointer only");
}

static node_t *new_node(gen_t *g, int kind, int line)
{
    node_t *n = cc_alloc(g->cc, sizeof *n);
    n->kind = (uint8_t)kind;
    n->line = line;
    return n;
}

static node_t *number(gen_t *g, int line, long v)
{
    node_t *n = new_node(g, N_NUM, line);
    make_constant(g, n, v);
    return n;
}

static ctype_t combine(const node_t *a, const node_t *b)
{
    ctype_t t = { CT_U8, 0, 0 };
    const int wide = width(a->type) == 2 || width(b->type) == 2;
    const int sign = is_signed(a) || is_signed(b);
    t.base = (uint8_t)(wide ? (sign ? CT_S16 : CT_U16) : (sign ? CT_S8 : CT_U8));
    return t;
}

/* The type of a binary node whose sides are read already; folds numbers and scales what goes with a pointer. */
static ctype_t binary_type(gen_t *g, node_t *n)
{
    node_t *a = n->a, *b = n->b;
    const int compare = n->op >= OP_EQ && n->op <= OP_GE;
    if (a->constant && b->constant) {
        make_constant(g, n, fold(g->cc, n->line, n->op, a->value, b->value));
        return n->type;
    }
    if (a->type.ptr || b->type.ptr) {
        if (compare)
            return (ctype_t){ CT_U8, 0, 0 };
        if (n->op == OP_ADD && !a->type.ptr) {
            n->a = b;
            n->b = a;
            a = n->a;
            b = n->b;
        }
        if ((n->op != OP_ADD && n->op != OP_SUB) || b->type.ptr)
            cc_error(g->cc, n->line, "with a pointer only + and - a number (and the comparisons)");
        if (element(a->type) == 2) {
            node_t *m = new_node(g, N_BINARY, b->line);
            m->op = OP_MUL;
            m->a = b;
            m->b = number(g, b->line, 2);
            m->type = (ctype_t){ CT_U16, 0, 0 };
            if (b->constant)
                make_constant(g, m, b->value * 2);
            n->b = m;
        }
        return a->type;
    }
    if (compare)
        return (ctype_t){ CT_U8, 0, 0 };
    if (n->op == OP_SHL || n->op == OP_SHR) {
        ctype_t t = a->type;
        if (a->constant)                    /* 1 << n may need 16 bits */
            t.base = CT_U16;
        return t;
    }
    return combine(a, b);
}

static void add_call(gen_t *g, sym_t *callee)
{
    sym_t *f = g->fn;
    if (!f)
        return;
    for (int i = 0; i < f->ncalls; i++)
        if (f->calls[i] == callee)
            return;
    if (f->ncalls == f->calls_max) {
        const int max = f->calls_max ? 2 * f->calls_max : 8;
        sym_t **c = cc_alloc(g->cc, (size_t)max * sizeof *c);
        if (f->ncalls)
            memcpy(c, f->calls, (size_t)f->ncalls * sizeof *c);
        f->calls = c;
        f->calls_max = max;
    }
    f->calls[f->ncalls++] = callee;
}

static int arg_width(const gen_t *g, int b, int i) { return g->snes ? builtins[b].snes[i] : builtins[b].nes[i]; }

static ctype_t call_type(gen_t *g, node_t *n)
{
    sym_t *s = find(g, n->name);
    if (!s)
        unknown(g, n->line, n->name);
    if (s->kind != SY_FUNC && s->kind != SY_BUILTIN)
        cc_error(g->cc, n->line, "'%s' is %s, not a function", n->name, what(s));
    n->sym = s;
    if (s->kind == SY_BUILTIN && !(builtins[s->builtin].on & (g->snes ? ON_SNES : ON_NES))) {
        if (s->builtin == B_BG_PAL)
            cc_error(g->cc, n->line, "background_palette is for the NES: on the SNES use palette(index, colour)");
        cc_error(g->cc, n->line, "'%s' is for the %s only", n->name, g->snes ? "NES" : "SNES");
    }
    if (s->kind == SY_BUILTIN && s->builtin == B_RGB) {
        make_constant(g, n, colour_of(g->cc, n));
        return n->type;
    }
    int count = 0;
    for (const node_t *a = n->a; a; a = a->next)
        count++;
    const int want = s->kind == SY_BUILTIN ? builtins[s->builtin].nargs : s->nparams;
    if (count != want) {
        if (s->kind == SY_BUILTIN)
            cc_error(g->cc, n->line, "%s%s takes %d argument%s, not %d", n->name, builtins[s->builtin].args, want,
                     want == 1 ? "" : "s", count);
        cc_error(g->cc, n->line, "'%s' (line %d) takes %d argument%s, not %d", n->name, s->line, want,
                 want == 1 ? "" : "s", count);
    }
    int i = 0;
    for (node_t *a = n->a; a; a = a->next, i++) {
        const ctype_t t = value_of(g, a);
        if (s->kind == SY_BUILTIN) {
            const int address = (s->builtin == B_PEEK || s->builtin == B_POKE) && i == 0;
            if (t.ptr && !address)
                cc_error(g->cc, a->line, "%s needs a number here, not an address", n->name);
            if (arg_width(g, s->builtin, i) == 1)
                fits(g, a, (ctype_t){ CT_U8, 0, 0 }, n->name);
        } else {
            const sym_t *p = s->params[i];
            if (p->type.ptr && !t.ptr && !(a->constant && !a->value))
                cc_error(g->cc, a->line, "'%s' of %s needs an address (an array, or &variable)", p->name, n->name);
            if (!p->type.ptr && t.ptr)
                cc_error(g->cc, a->line, "'%s' of %s is a number, not an address", p->name, n->name);
            fits(g, a, p->type, p->name);
        }
    }
    if (s->kind == SY_FUNC) {
        add_call(g, s);
        return s->type;
    }
    return (ctype_t){ (uint8_t)builtins[s->builtin].ret, 0, 0 };
}

static ctype_t expr(gen_t *g, node_t *n)
{
    ctype_t t = { CT_U8, 0, 0 };
    switch (n->kind) {
    case N_NUM:
        make_constant(g, n, n->value);
        return n->type;
    case N_NAME: {
        sym_t *s = find(g, n->name);
        if (!s)
            unknown(g, n->line, n->name);
        if (s->kind == SY_CONST) {
            make_constant(g, n, s->value);
            return n->type;
        }
        if (s->kind == SY_FUNC || s->kind == SY_BUILTIN)
            cc_error(g->cc, n->line, "'%s' is a function: call it with %s(...)", n->name, n->name);
        n->sym = s;
        t = s->type;
        if (t.len) {                            /* an array gives its address */
            t.len = 0;
            t.ptr++;
        }
        break;
    }
    case N_UNARY: {
        const ctype_t a = value_of(g, n->a);
        if (a.ptr && n->op != OP_NOT)
            cc_error(g->cc, n->line, "this operator does not work on an address");
        if (n->a->constant) {
            long v = fold(g->cc, n->line, n->op, n->a->value, 0);
            if (n->op == OP_INV)
                v &= width(a) == 2 ? 0xFFFF : 0xFF;
            make_constant(g, n, v);
            return n->type;
        }
        if (n->op == OP_NEG)
            t.base = width(a) == 2 ? CT_S16 : CT_S8;
        else if (n->op == OP_INV)
            t = a;
        break;
    }
    case N_BINARY:
        value_of(g, n->a);
        value_of(g, n->b);
        t = binary_type(g, n);
        if (n->constant)
            return n->type;
        break;
    case N_AND:
    case N_OR:
        value_of(g, n->a);
        value_of(g, n->b);
        if (n->a->constant && n->b->constant) {
            make_constant(g, n, n->kind == N_AND ? n->a->value && n->b->value : n->a->value || n->b->value);
            return n->type;
        }
        break;
    case N_COND:
        value_of(g, n->a);
        value_of(g, n->b);
        value_of(g, n->c);
        if (n->a->constant && n->b->constant && n->c->constant) {
            make_constant(g, n, n->a->value ? n->b->value : n->c->value);
            return n->type;
        }
        t = n->b->type.ptr ? n->b->type : n->c->type.ptr ? n->c->type : combine(n->b, n->c);
        break;
    case N_ASSIGN: {
        t = value_of(g, n->a);
        writable(g, n->a);
        value_of(g, n->b);
        if (n->op != OP_NONE) {                 /* a += b is a = a + b */
            if (n->a->kind != N_NAME && has_effect(n->a))
                cc_error(g->cc, n->line, "put the index in a variable first, then change the element");
            node_t *m = new_node(g, N_BINARY, n->line);
            m->op = n->op;
            m->a = n->a;
            m->b = n->b;
            m->type = binary_type(g, m);
            n->b = m;
            n->op = OP_NONE;
        } else if (t.ptr && !n->b->type.ptr && !(n->b->constant && !n->b->value))
            cc_error(g->cc, n->line, "a pointer takes an address: an array, or &variable");
        else if (!t.ptr && n->b->type.ptr)
            cc_error(g->cc, n->line, "an address does not go in a number: declare a pointer (byte *p)");
        if (!n->b->type.ptr)
            fits(g, n->b, t, n->a->kind == N_NAME ? n->a->name : "this element");
        break;
    }
    case N_PREINC:
    case N_POSTINC: {
        t = value_of(g, n->a);
        writable(g, n->a);
        if (n->a->kind != N_NAME && has_effect(n->a))
            cc_error(g->cc, n->line, "put the index in a variable first, then change the element");
        node_t *m = new_node(g, N_BINARY, n->line);
        m->op = n->op;
        m->a = n->a;
        m->b = number(g, n->line, 1);
        m->type = binary_type(g, m);
        n->b = new_node(g, N_ASSIGN, n->line);
        n->b->a = n->a;
        n->b->b = m;
        n->b->type = t;
        break;
    }
    case N_INDEX: {
        const ctype_t a = value_of(g, n->a);
        const ctype_t i = value_of(g, n->b);
        if (!a.ptr)
            cc_error(g->cc, n->line, "'%s' is not an array or a pointer", n->a->kind == N_NAME ? n->a->name : "this");
        if (i.ptr)
            cc_error(g->cc, n->line, "an index is a number");
        const sym_t *s = n->a->kind == N_NAME ? n->a->sym : NULL;
        if (s && s->type.len && n->b->constant && (n->b->value < 0 || n->b->value >= s->type.len))
            cc_error(g->cc, n->line, "index %ld is outside '%s' (0 to %d)", n->b->value, s->name, s->type.len - 1);
        t = a;
        t.ptr--;
        break;
    }
    case N_DEREF: {
        t = value_of(g, n->a);
        if (!t.ptr)
            cc_error(g->cc, n->line, "'*' needs a pointer (an address)");
        t.ptr--;
        if (!t.ptr && t.base == CT_VOID)
            cc_error(g->cc, n->line, "a void pointer has no value: cast it to (byte *)");
        break;
    }
    case N_ADDR: {
        t = value_of(g, n->a);
        if (n->a->kind == N_NAME && n->a->sym && n->a->sym->type.len)
            break;                              /* &array: the address of its first value */
        if (!(n->a->kind == N_INDEX || n->a->kind == N_DEREF || (n->a->kind == N_NAME && n->a->sym)))
            cc_error(g->cc, n->line, "'&' needs a variable or an array element");
        if (++t.ptr > 2)
            cc_error(g->cc, n->line, "two '*' at most");
        break;
    }
    case N_CAST:
        value_of(g, n->a);
        t = n->type;
        if (n->a->constant && !t.ptr) {
            long v = width(t) == 2 ? n->a->value & 0xFFFF : n->a->value & 0xFF;
            if (t.base == CT_S8)
                v = (int8_t)v;
            else if (t.base == CT_S16)
                v = (int16_t)v;
            make_constant(g, n, v);
            n->type = t;
            return t;
        }
        break;
    case N_CALL:
        t = call_type(g, n);
        if (n->constant)
            return n->type;
        break;
    default:
        cc_error(g->cc, n->line, "this cannot be a value");
    }
    n->type = t;
    return t;
}

/* ---- analysis: statements and declarations ---- */

static void statement_check(gen_t *g, node_t *n, sym_t *block_start);

/* A variable (global or local): its symbol, place and first value. */
static void declare_variable(gen_t *g, node_t *d, sym_t *block_start)
{
    const int local = g->fn != NULL;
    if (local) {
        for (const sym_t *s = g->locals; s && s != block_start; s = s->next)
            if (!strcmp(s->name, d->name))
                cc_error(g->cc, d->line, "'%s' is already declared in this block, at line %d", d->name, s->line);
    }
    int count = 0;
    if (d->b) {                                 /* the size of an array: a number or a constant */
        const long len = cc_constant(g->cc, d->b);
        if (len < 1 || len > 2048)
            cc_error(g->cc, d->line, "an array holds 1 to 2048 values");
        d->type.len = (int)len;
    }
    if (d->a && d->a->kind == N_LIST) {
        if (!d->type.len)
            cc_error(g->cc, d->line, "'%s' is one value: a list { } needs an array, %s[]", d->name, d->name);
        for (node_t *v = d->a->a; v; v = v->next, count++) {
            value_of(g, v);
            fits(g, v, (ctype_t){ d->type.base, d->type.ptr, 0 }, d->name);
        }
        if (d->type.len < 0)
            d->type.len = count;
        if (!d->type.len)
            cc_error(g->cc, d->line, "'%s' needs values or a size", d->name);
        if (count > d->type.len)
            cc_error(g->cc, d->line, "'%s' holds %d values, the list has %d", d->name, d->type.len, count);
    } else if (d->a) {
        if (d->type.len)
            cc_error(g->cc, d->line, "an array takes its values as a list: { 1, 2, 3 }");
        value_of(g, d->a);
        if (!d->type.ptr && d->a->type.ptr)
            cc_error(g->cc, d->line, "an address does not go in a number: declare a pointer (byte *%s)", d->name);
        fits(g, d->a, d->type, d->name);
    }
    if (d->type.len < 0)
        cc_error(g->cc, d->line, "'%s' needs a size or a list of values", d->name);
    const int is_const = d->op && !d->type.ptr;
    sym_t *s;
    if (is_const && !d->type.len) {             /* a constant takes no RAM */
        if (!d->a || !d->a->constant)
            cc_error(g->cc, d->line, "const %s needs a number that is known when the program is built", d->name);
        s = new_sym(g, d->name, SY_CONST, d->line);
        s->value = d->a->value;
        s->type = d->type;
    } else {
        s = new_sym(g, d->name, is_const ? SY_ROM : SY_VAR, d->line);
        s->type = d->type;
        s->is_const = (uint8_t)is_const;
        s->init = d->a;
        if (is_const && !d->a)
            cc_error(g->cc, d->line, "const %s[] needs its values", d->name);
        if (is_const || !local)
            for (const node_t *v = d->a && d->a->kind == N_LIST ? d->a->a : d->a; v;
                 v = d->a->kind == N_LIST ? v->next : NULL)
                if (!v->constant)
                    cc_error(g->cc, v->line, "the first value of '%s' is a number known when the program is built "
                             "(set it in start() otherwise)", d->name);
        make_label(g, s, is_const ? "d_" : local ? "l_" : "v_", local ? g->fn->name : NULL);
        keep(g, s);
        if (!is_const)
            place(g, s);
        if (!local && !is_const && d->a && bytes_of(s) > 256)
            cc_error(g->cc, d->line, "'%s' has first values for more than 256 bytes: fill it in start()", d->name);
    }
    d->sym = s;
    if (local) {
        s->next = g->locals;
        g->locals = s;
    } else
        declare_global(g, s);
}

static void check_asm(gen_t *g, const node_t *n)
{
    for (const node_t *s = n->a; s; s = s->next)
        for (const char *p = strchr(s->name, '{'); p; p = strchr(p + 1, '{')) {
            const char *end = strchr(p, '}');
            char name[48];
            if (!end || end - p - 1 >= (int)sizeof name || end == p + 1)
                cc_error(g->cc, s->line, "in asm, {name} puts the place of a variable: close it with }");
            memcpy(name, p + 1, (size_t)(end - p - 1));
            name[end - p - 1] = 0;
            const sym_t *v = find(g, name);
            if (!v)
                unknown(g, s->line, name);
            if (v->kind != SY_VAR && v->kind != SY_ROM && v->kind != SY_FUNC && v->kind != SY_CONST)
                cc_error(g->cc, s->line, "{%s}: only variables, constants and functions have a place", name);
        }
}

/* The statements of a block, or the one statement of an if or a loop, in a scope of its own. */
static void scoped(gen_t *g, node_t *n)
{
    sym_t *start = g->locals;
    if (n->kind == S_BLOCK)
        for (node_t *s = n->a; s; s = s->next)
            statement_check(g, s, start);
    else
        statement_check(g, n, start);
    g->locals = start;
}

static void statement_check(gen_t *g, node_t *n, sym_t *block_start)
{
    switch (n->kind) {
    case S_BLOCK:
        scoped(g, n);
        break;
    case S_DECL:
        declare_variable(g, n, block_start);
        break;
    case S_IF:
        value_of(g, n->a);
        scoped(g, n->b);
        if (n->c)
            scoped(g, n->c);
        break;
    case S_WHILE:
    case S_DO:
        value_of(g, n->a);
        g->loops++;
        scoped(g, n->b);
        g->loops--;
        break;
    case S_FOR: {
        sym_t *start = g->locals;
        for (node_t *s = n->a; s; s = s->next)
            statement_check(g, s, start);
        if (n->b)
            value_of(g, n->b);
        if (n->c)
            expr(g, n->c);
        g->loops++;
        scoped(g, n->d);
        g->loops--;
        g->locals = start;
        break;
    }
    case S_BREAK:
    case S_CONTINUE:
        if (!g->loops)
            cc_error(g->cc, n->line, "%s works only inside while, do and for",
                     n->kind == S_BREAK ? "break" : "continue");
        break;
    case S_RETURN: {
        const int gives = g->fn->type.base != CT_VOID || g->fn->type.ptr;
        if (n->a && !gives)
            cc_error(g->cc, n->line, "'%s' is void: write return; with no value", g->fn->name);
        if (!n->a && gives)
            cc_error(g->cc, n->line, "'%s' gives a value: write return value;", g->fn->name);
        if (n->a) {
            value_of(g, n->a);
            fits(g, n->a, g->fn->type, g->fn->name);
        }
        break;
    }
    case S_EXPR:
        expr(g, n->a);
        break;
    case S_ASM:
        check_asm(g, n);
        break;
    default:
        break;
    }
}

static void recursion(gen_t *g, sym_t *f)
{
    f->state = 1;
    for (int i = 0; i < f->ncalls; i++) {
        sym_t *c = f->calls[i];
        if (c->state == 1)
            cc_error(g->cc, f->line, "'%s' calls '%s', which leads back to it: bm C gives the locals fixed places, "
                     "so a function cannot call itself (use a loop)", f->name, c->name);
        if (!c->state)
            recursion(g, c);
    }
    f->state = 2;
}

static void analyse(gen_t *g)
{
    /* the functions first: they can be called before their text */
    for (node_t *d = g->prog.decls; d; d = d->next)
        if (d->kind == S_BLOCK) {
            sym_t *f = new_sym(g, d->name, SY_FUNC, d->line);
            f->type = d->type;
            f->body = d;
            d->sym = f;
            declare_global(g, f);
            snprintf(f->label, sizeof f->label, "fn_%s", f->name);
            keep(g, f);
            for (node_t *p = d->b; p; p = p->next) {
                sym_t *s = new_sym(g, p->name, SY_VAR, p->line);
                s->type = p->type;
                s->param = 1;
                for (int i = 0; i < f->nparams; i++)
                    if (!strcmp(f->params[i]->name, p->name))
                        cc_error(g->cc, p->line, "'%s' has two parameters named '%s'", f->name, p->name);
                f->params[f->nparams++] = s;
                make_label(g, s, "l_", f->name);
                keep(g, s);
                place(g, s);
            }
        }
    for (node_t *d = g->prog.decls; d; d = d->next)
        if (d->kind == S_DECL)
            declare_variable(g, d, NULL);
    for (node_t *d = g->prog.decls; d; d = d->next)
        if (d->kind == S_BLOCK) {
            g->fn = d->sym;
            g->locals = NULL;
            for (int i = 0; i < g->fn->nparams; i++) {
                g->fn->params[i]->next = g->locals;
                g->locals = g->fn->params[i];
            }
            for (node_t *s = d->a->a; s; s = s->next)
                statement_check(g, s, NULL);
            g->locals = NULL;
            g->fn = NULL;
        }
    for (sym_t *s = g->all; s; s = s->all)
        if (s->kind == SY_FUNC && !s->state)
            recursion(g, s);

    const sym_t *entry[3];
    static const char *const names[3] = { "start", "update", "main" };
    for (int i = 0; i < 3; i++) {
        entry[i] = find_in(g->globals, names[i]);
        if (entry[i] && entry[i]->kind != SY_FUNC)
            cc_error(g->cc, entry[i]->line, "'%s' is the name of a function of the program: choose another", names[i]);
        if (entry[i] && (entry[i]->nparams || entry[i]->type.base != CT_VOID || entry[i]->type.ptr))
            cc_error(g->cc, entry[i]->line, "write it as void %s(void)", names[i]);
    }
    if (entry[1] && entry[2])
        cc_error(g->cc, entry[2]->line, "write main() or update(), not both: update() runs once every frame");
    if (!entry[1] && !entry[2])
        cc_error(g->cc, 1, "add void update(void) { }: it runs once every frame");
}

/* ---- the output ---- */

static void put(gen_t *g, const char *s, size_t n)
{
    if (g->len + n + 1 > g->max) {
        size_t max = g->max ? g->max : 16384;
        while (g->len + n + 1 > max)
            max *= 2;
        if (max > 4 * 1024 * 1024)
            cc_error(g->cc, g->line, "the program is too large");
        char *t = realloc(g->text, max);
        if (!t)
            cc_error(g->cc, g->line, "not enough memory to compile");
        g->text = t;
        g->max = max;
    }
    for (size_t i = 0; i < n; i++) {
        g->text[g->len++] = s[i];
        if (s[i] != '\n')
            continue;
        if (g->lines == g->lines_max) {
            const int max = g->lines_max ? 2 * g->lines_max : 1024;
            int *l = realloc(g->line_of, (size_t)max * sizeof *l);
            if (!l)
                cc_error(g->cc, g->line, "not enough memory to compile");
            g->line_of = l;
            g->lines_max = max;
        }
        g->line_of[g->lines++] = g->line;
    }
    g->text[g->len] = 0;
}

static void emit(gen_t *g, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void emit(gen_t *g, const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(b, sizeof b - 1, fmt, ap);
    va_end(ap);
    const size_t len = n < 0 ? 0 : (size_t)n < sizeof b - 2 ? (size_t)n : sizeof b - 2;
    b[len] = '\n';
    put(g, b, len + 1);
}

/* One instruction. */
#define I(...) emit(g, "    " __VA_ARGS__)

static int new_label(gen_t *g) { return ++g->labels; }
static void label(gen_t *g, int n) { emit(g, "L%d:", n); }

/* The comment with the line of C, once for each line. */
static void comment(gen_t *g, int line)
{
    g->line = line;
    if (line == g->commented || line <= 0)
        return;
    g->commented = line;
    size_t p = 0;
    for (int n = 1; n < line && p < g->cc->len; p++)
        n += g->cc->src[p] == '\n';
    while (p < g->cc->len && (g->cc->src[p] == ' ' || g->cc->src[p] == '\t'))
        p++;
    size_t e = p;
    while (e < g->cc->len && g->cc->src[e] != '\n' && g->cc->src[e] != '\r')
        e++;
    emit(g, "; main.c:%d  %.*s", line, e - p > 90 ? 90 : (int)(e - p), g->cc->src + p);
}

/* ---- the 6502 ---- */

static void gen(gen_t *g, node_t *n, int w);
static void cond(gen_t *g, node_t *n, int target, int when);
static void assign(gen_t *g, node_t *n, int used, int w);
static void address(gen_t *g, node_t *n);
static void call(gen_t *g, node_t *n);

static void immediate(gen_t *g, long v, int w)
{
    I("lda #$%02X", (unsigned)(v & 0xFF));
    if (w == 2)
        I("ldx #$%02X", (unsigned)((v >> 8) & 0xFF));
}

/* A value of `from` bytes in A (X) made `to` bytes wide. */
static void convert(gen_t *g, int from, int sign, int to)
{
    if (to <= from)
        return;
    I("ldx #0");
    if (sign) {
        const int l = new_label(g);
        I("cmp #$80");
        I("bcc L%d", l);
        I("dex");
        label(g, l);
    }
}

/* 1 when a value of w bytes can be the operand of an instruction as it is (no code to make it). */
static int simple(const node_t *n, int w)
{
    if (n->constant)
        return 1;
    if (n->kind != N_NAME || !n->sym)
        return 0;
    if (n->sym->type.len)
        return 1;
    return width(n->type) >= w || !signed_type(n->type);
}

/* The operand of byte `high` of such a value. */
static const char *operand(const node_t *n, int high, char buf[2][112])
{
    char *b = buf[high];
    if (n->constant)
        snprintf(b, 112, "#$%02X", (unsigned)((n->value >> (8 * high)) & 0xFF));
    else if (n->sym->type.len)
        snprintf(b, 112, "#%c%s", high ? '>' : '<', n->sym->label);
    else if (!high)
        snprintf(b, 112, "%s", n->sym->label);
    else if (width(n->type) == 2)
        snprintf(b, 112, "%s+1", n->sym->label);
    else
        snprintf(b, 112, "#0");
    return b;
}

static void push(gen_t *g, int w)
{
    I("pha");
    if (w == 2) {
        I("txa");
        I("pha");
    }
}

/* What was pushed goes to __b; A and X stay. */
static void pop_b(gen_t *g, int w)
{
    I("tay");
    I("pla");
    if (w == 2) {
        I("sta __b+1");
        I("pla");
    }
    I("sta __b");
    I("tya");
}

/* A (X) op the operand lo (hi): + - & | ^ */
static void apply(gen_t *g, int op, const char *lo, const char *hi, int w)
{
    const char *m = op == OP_ADD ? "adc" : op == OP_SUB ? "sbc" : op == OP_AND ? "and" : op == OP_OR ? "ora" : "eor";
    if (op == OP_ADD)
        I("clc");
    else if (op == OP_SUB)
        I("sec");
    I("%s %s", m, lo);
    if (w == 2) {
        I("tay");
        I("txa");
        I("%s %s", m, hi);
        I("tax");
        I("tya");
    }
}

static int power_of_two(const node_t *n)
{
    if (!n->constant || n->value <= 0)
        return -1;
    for (int k = 0; k < 16; k++)
        if (n->value == 1L << k)
            return k;
    return -1;
}

static void shift_by(gen_t *g, int left, int sign, long k, int w)
{
    if (k >= 8 * w) {
        if (!left && sign)
            k = 8 * w - 1;
        else {
            immediate(g, 0, w);
            return;
        }
    }
    if (!k)
        return;
    if (w == 1) {
        for (long i = 0; i < k; i++) {
            if (left)
                I("asl a");
            else if (sign) {
                I("cmp #$80");
                I("ror a");
            } else
                I("lsr a");
        }
        return;
    }
    I("sta __a");
    I("stx __a+1");
    for (long i = 0; i < k; i++) {
        if (left) {
            I("asl __a");
            I("rol __a+1");
        } else {
            if (sign) {
                I("lda __a+1");
                I("cmp #$80");
                I("ror __a+1");
            } else
                I("lsr __a+1");
            I("ror __a");
        }
    }
    I("lda __a");
    I("ldx __a+1");
}

/* a and b of 16 bits into __a and __b. */
static void both_sides(gen_t *g, node_t *a, node_t *b)
{
    gen(g, b, 2);
    push(g, 2);
    gen(g, a, 2);
    pop_b(g, 2);
    I("sta __a");
    I("stx __a+1");
}

static void arithmetic(gen_t *g, node_t *n, int w)
{
    node_t *a = n->a, *b = n->b;
    const int op = n->op;
    const int wc = width(n->type) > w ? width(n->type) : w;
    char buf[2][112];
    if (op == OP_MUL || op == OP_DIV || op == OP_MOD) {
        const int sign = is_signed(a) || is_signed(b);
        if (op == OP_MUL && power_of_two(a) >= 0) {
            node_t *t = a;
            a = b;
            b = t;
        }
        const int k = power_of_two(b);
        if (k >= 0 && (op == OP_MUL || !sign)) {
            gen(g, a, wc);
            if (op == OP_MOD) {
                const long mask = b->value - 1;
                I("and #$%02X", (unsigned)(mask & 0xFF));
                if (wc == 2) {
                    I("tay");
                    I("txa");
                    I("and #$%02X", (unsigned)(mask >> 8 & 0xFF));
                    I("tax");
                    I("tya");
                }
            } else
                shift_by(g, op == OP_MUL, 0, k, wc);
            return;
        }
        both_sides(g, a, b);
        if (op == OP_MUL) {
            g->parts |= RT_MUL;
            I("jsr __mul");
        } else {
            g->parts |= sign ? RT_SDIV | RT_UDIV : RT_UDIV;
            I("jsr %s", sign ? "__sdiv" : "__udiv");
        }
        const char *from = op == OP_MOD ? "__r" : "__a";
        I("lda %s", from);
        if (w == 2)
            I("ldx %s+1", from);
        return;
    }
    if (op == OP_SHL || op == OP_SHR) {
        const int sign = is_signed(a);
        if (b->constant) {
            gen(g, a, wc);
            shift_by(g, op == OP_SHL, sign, b->value, wc);
            return;
        }
        gen(g, b, 1);
        push(g, 1);
        gen(g, a, 2);
        pop_b(g, 1);
        I("sta __a");
        I("stx __a+1");
        g->parts |= RT_SHIFT;
        I("jsr %s", op == OP_SHL ? "__shl" : sign ? "__sar" : "__shr");
        I("lda __a");
        if (w == 2)
            I("ldx __a+1");
        return;
    }
    if (simple(b, wc)) {
        gen(g, a, wc);
        const char *lo = operand(b, 0, buf);
        apply(g, op, lo, wc == 2 ? operand(b, 1, buf) : "", wc);
    } else if (op != OP_SUB && simple(a, wc)) {
        gen(g, b, wc);
        const char *lo = operand(a, 0, buf);
        apply(g, op, lo, wc == 2 ? operand(a, 1, buf) : "", wc);
    } else {
        gen(g, b, wc);
        push(g, wc);
        gen(g, a, wc);
        pop_b(g, wc);
        apply(g, op, "__b", "__b+1", wc);
    }
}

/* Jumps to target when the comparison is `when`. */
static void compare(gen_t *g, node_t *n, int target, int when)
{
    node_t *a = n->a, *b = n->b;
    int op = n->op;
    int w = width(a->type) > width(b->type) ? width(a->type) : width(b->type);
    const int sign = is_signed(a) || is_signed(b);
    if (sign && w == 1) {               /* a byte above 127 beside a signed one: compare in 16 bits */
        const node_t *side[2] = { a, b };
        for (int i = 0; i < 2; i++)
            if (!is_signed(side[i]) && (!side[i]->constant || side[i]->value > 127))
                w = 2;
    }
    if (op == OP_GT || op == OP_LE) {   /* a > b is b < a */
        node_t *t = a;
        a = b;
        b = t;
        op = op == OP_GT ? OP_LT : OP_GE;
    }
    char buf[2][112];
    const char *lo = "__b", *hi = "__b+1";
    if (simple(b, w)) {
        gen(g, a, w);
        lo = operand(b, 0, buf);
        if (w == 2)
            hi = operand(b, 1, buf);
    } else {
        gen(g, b, w);
        push(g, w);
        gen(g, a, w);
        pop_b(g, w);
    }
    const int equality = op == OP_EQ || op == OP_NE;
    if (w == 1 && (equality || !sign))
        I("cmp %s", lo);
    else if (w == 1) {
        const int l = new_label(g);
        I("sec");
        I("sbc %s", lo);
        I("bvc L%d", l);
        I("eor #$80");
        label(g, l);
    } else if (equality) {
        const int l = new_label(g);
        I("cmp %s", lo);
        I("bne L%d", l);
        I("cpx %s", hi);
        label(g, l);
    } else {
        I("cmp %s", lo);
        I("txa");
        I("sbc %s", hi);
        if (sign) {
            const int l = new_label(g);
            I("bvc L%d", l);
            I("eor #$80");
            label(g, l);
        }
    }
    /* the branch taken when the comparison holds, and the one taken when it does not */
    const char *yes = op == OP_EQ ? "beq" : op == OP_NE ? "bne" : op == OP_LT ? (sign ? "bmi" : "bcc") :
                      (sign ? "bpl" : "bcs");
    const char *no = op == OP_EQ ? "bne" : op == OP_NE ? "beq" : op == OP_LT ? (sign ? "bpl" : "bcs") :
                     (sign ? "bmi" : "bcc");
    const int skip = new_label(g);
    I("%s L%d", when ? no : yes, skip);
    I("jmp L%d", target);
    label(g, skip);
}

static void cond(gen_t *g, node_t *n, int target, int when)
{
    if (n->constant) {
        if ((n->value != 0) == when)
            I("jmp L%d", target);
        return;
    }
    if (n->kind == N_UNARY && n->op == OP_NOT) {
        cond(g, n->a, target, !when);
        return;
    }
    if (n->kind == N_AND || n->kind == N_OR) {
        const int both = (n->kind == N_AND) != when;        /* either side alone decides the jump */
        if (both) {
            cond(g, n->a, target, when);
            cond(g, n->b, target, when);
        } else {
            const int skip = new_label(g);
            cond(g, n->a, skip, !when);
            cond(g, n->b, target, when);
            label(g, skip);
        }
        return;
    }
    if (n->kind == N_BINARY && n->op >= OP_EQ && n->op <= OP_GE) {
        compare(g, n, target, when);
        return;
    }
    const int w = width(n->type);
    gen(g, n, w);
    if (w == 2) {
        I("stx __b");
        I("ora __b");
    } else
        I("cmp #0");
    const int skip = new_label(g);
    I("%s L%d", when ? "beq" : "bne", skip);
    I("jmp L%d", target);
    label(g, skip);
}

/* A comparison or a logic expression as a value, 0 or 1. */
static void truth(gen_t *g, node_t *n, int w)
{
    const int yes = new_label(g), end = new_label(g);
    cond(g, n, yes, 1);
    I("lda #0");
    I("jmp L%d", end);
    label(g, yes);
    I("lda #1");
    label(g, end);
    if (w == 2)
        I("ldx #0");
}

/* Where an element is: a fixed place, a fixed array and an index in X, or an address to compute. */
enum { LV_FIXED, LV_INDEXED, LV_ADDRESS };

static int place_of(const node_t *n, char *at, size_t len)
{
    if (n->kind == N_NAME) {
        snprintf(at, len, "%s", n->sym->label);
        return LV_FIXED;
    }
    if (n->kind == N_INDEX && n->a->kind == N_NAME && n->a->sym && n->a->sym->type.len) {
        const sym_t *s = n->a->sym;
        const int size = element(s->type);
        if (n->b->constant) {
            snprintf(at, len, "%s+%ld", s->label, n->b->value * size);
            return LV_FIXED;
        }
        if (width(n->b->type) == 1 && !signed_type(n->b->type) && s->type.len * size <= 256) {
            snprintf(at, len, "%s", s->label);
            return LV_INDEXED;
        }
    }
    return LV_ADDRESS;
}

static void index_to_x(gen_t *g, node_t *i, int size)
{
    gen(g, i, 1);
    if (size == 2)
        I("asl a");
    I("tax");
}

static void add_to_address(gen_t *g, const char *lo, const char *hi)
{
    apply(g, OP_ADD, lo, hi, 2);
}

/* A (X) = the address of a place. */
static void address(gen_t *g, node_t *n)
{
    char lo[112], hi[112];
    if (n->kind == N_NAME) {
        I("lda #<%s", n->sym->label);
        I("ldx #>%s", n->sym->label);
        return;
    }
    if (n->kind == N_DEREF) {
        gen(g, n->a, 2);
        return;
    }
    const sym_t *s = n->a->kind == N_NAME && n->a->sym && n->a->sym->type.len ? n->a->sym : NULL;
    ctype_t holds = n->a->type;
    const int size = element(holds);
    if (n->b->constant) {
        const long off = (n->b->value * size) & 0xFFFF;
        if (s) {
            I("lda #<%s+%ld", s->label, off);
            I("ldx #>%s+%ld", s->label, off);
            return;
        }
        gen(g, n->a, 2);
        snprintf(lo, sizeof lo, "#$%02X", (unsigned)(off & 0xFF));
        snprintf(hi, sizeof hi, "#$%02X", (unsigned)(off >> 8));
        add_to_address(g, lo, hi);
        return;
    }
    gen(g, n->b, 2);
    if (size == 2)
        shift_by(g, 1, 0, 1, 2);
    if (s) {
        snprintf(lo, sizeof lo, "#<%s", s->label);
        snprintf(hi, sizeof hi, "#>%s", s->label);
        add_to_address(g, lo, hi);
        return;
    }
    push(g, 2);
    gen(g, n->a, 2);
    pop_b(g, 2);
    add_to_address(g, "__b", "__b+1");
}

/* The value of an element or of *pointer, of its own width. */
static void load(gen_t *g, node_t *n)
{
    char at[112];
    const int w = width(n->type);
    switch (place_of(n, at, sizeof at)) {
    case LV_FIXED:
        I("lda %s", at);
        if (w == 2)
            I("ldx %s+1", at);
        break;
    case LV_INDEXED:
        index_to_x(g, n->b, w);
        if (w == 1)
            I("lda %s,x", at);
        else {
            I("lda %s,x", at);
            I("pha");
            I("lda %s+1,x", at);
            I("tax");
            I("pla");
        }
        break;
    default:
        address(g, n);
        I("sta __p");
        I("stx __p+1");
        I("ldy #0");
        I("lda (__p),y");
        if (w == 2) {
            I("pha");
            I("iny");
            I("lda (__p),y");
            I("tax");
            I("pla");
        }
        break;
    }
}

static void assign(gen_t *g, node_t *n, int used, int w)
{
    node_t *to = n->a, *v = n->b;
    const int tw = width(to->type);
    char at[112];
    switch (place_of(to, at, sizeof at)) {
    case LV_FIXED:
        gen(g, v, tw);
        I("sta %s", at);
        if (tw == 2)
            I("stx %s+1", at);
        break;
    case LV_INDEXED:
        gen(g, v, tw);
        push(g, tw);
        index_to_x(g, to->b, tw);
        if (tw == 1) {
            I("pla");
            I("sta %s,x", at);
        } else {
            I("pla");
            I("sta %s+1,x", at);
            I("pla");
            I("sta %s,x", at);
            if (used) {
                I("pha");
                I("lda %s+1,x", at);
                I("tax");
                I("pla");
            }
        }
        break;
    default:
        address(g, to);
        push(g, 2);
        gen(g, v, tw);
        if (tw == 1)
            I("tay");
        else {
            I("sta __r");
            I("stx __r+1");
        }
        I("pla");
        I("sta __p+1");
        I("pla");
        I("sta __p");
        if (tw == 1) {
            I("tya");
            I("ldy #0");
            I("sta (__p),y");
        } else {
            I("ldy #0");
            I("lda __r");
            I("sta (__p),y");
            I("iny");
            I("lda __r+1");
            I("sta (__p),y");
            I("lda __r");
            I("ldx __r+1");
        }
        break;
    }
    if (used)
        convert(g, tw, signed_type(to->type), w);
}

static void step(gen_t *g, node_t *n, int used, int w)
{
    node_t *to = n->a;
    const int tw = width(to->type), post = n->kind == N_POSTINC;
    const int up = n->op == OP_ADD;
    char at[112];
    if (place_of(to, at, sizeof at) == LV_FIXED && !to->type.ptr) {
        if (used && post) {
            I("lda %s", at);
            I("pha");
            if (tw == 2) {
                I("lda %s+1", at);
                I("pha");
            }
        }
        if (tw == 1)
            I("%s %s", up ? "inc" : "dec", at);
        else {
            const int l = new_label(g);
            if (up) {
                I("inc %s", at);
                I("bne L%d", l);
                I("inc %s+1", at);
                label(g, l);
            } else {
                I("lda %s", at);
                I("bne L%d", l);
                I("dec %s+1", at);
                label(g, l);
                I("dec %s", at);
            }
        }
        if (used && post) {
            if (tw == 2) {
                I("pla");
                I("tax");
            }
            I("pla");
        } else if (used) {
            I("lda %s", at);
            if (tw == 2)
                I("ldx %s+1", at);
        }
    } else if (used && post) {
        gen(g, to, tw);
        push(g, tw);
        assign(g, n->b, 0, tw);
        if (tw == 2) {
            I("pla");
            I("tax");
        }
        I("pla");
    } else
        assign(g, n->b, used, tw);
    if (used)
        convert(g, tw, signed_type(to->type), w);
}

/* The result stays in A (X), of the width of the function. */
static void call(gen_t *g, node_t *n)
{
    const sym_t *s = n->sym;
    node_t *a = n->a;
    if (s->kind == SY_BUILTIN) {
        const int b = s->builtin;
        g->parts |= builtins[b].part;
        switch (b) {
        case B_HELD:
        case B_PRESSED: {
            const char *from = b == B_HELD ? "__pad" : "__new";
            if (!g->snes) {
                gen(g, a, 1);
                I("and %s", from);
                return;
            }
            gen(g, a, 2);                       /* the 12 buttons: not 0 in A when one of them is */
            I("and %s", from);
            I("sta __a");
            I("txa");
            I("and %s+1", from);
            I("ora __a");
            return;
        }
        case B_PEEK:
            gen(g, a, 2);
            I("sta __p");
            I("stx __p+1");
            I("ldy #0");
            I("lda (__p),y");
            return;
        case B_POKE:
            gen(g, a, 2);
            push(g, 2);
            gen(g, a->next, 1);
            I("tay");
            I("pla");
            I("sta __p+1");
            I("pla");
            I("sta __p");
            I("tya");
            I("ldy #0");
            I("sta (__p),y");
            return;
        case B_SONG:
        case B_SFX:
            gen(g, a, 1);
            I("jsr %s", builtins[b].routine);
            return;
        default:
            break;
        }
    }
    const int count = s->kind == SY_BUILTIN ? builtins[s->builtin].nargs : s->nparams;
    int inner = 0;                          /* an argument that calls would write over the places */
    for (const node_t *v = a; v; v = v->next)
        inner |= has_effect(v);
    char slot[CC_PARAMS][112];
    int w[CC_PARAMS];
    for (int i = 0, at = 0; i < count; i++) {
        if (s->kind == SY_BUILTIN) {
            snprintf(slot[i], sizeof slot[i], "__arg+%d", at);
            w[i] = arg_width(g, s->builtin, i);
            at += w[i];
        } else {
            snprintf(slot[i], sizeof slot[i], "%s", s->params[i]->label);
            w[i] = width(s->params[i]->type);
        }
    }
    int i = 0;
    for (node_t *v = a; v; v = v->next, i++) {
        gen(g, v, w[i]);
        if (inner && count > 1)
            push(g, w[i]);
        else {
            I("sta %s", slot[i]);
            if (w[i] == 2)
                I("stx %s+1", slot[i]);
        }
    }
    if (inner && count > 1)
        for (i = count - 1; i >= 0; i--) {
            I("pla");
            if (w[i] == 2) {
                I("sta %s+1", slot[i]);
                I("pla");
            }
            I("sta %s", slot[i]);
        }
    I("jsr %s", s->kind == SY_BUILTIN ? builtins[s->builtin].routine : s->label);
}

static void gen(gen_t *g, node_t *n, int w)
{
    if (n->constant) {
        immediate(g, n->value, w);
        return;
    }
    switch (n->kind) {
    case N_NAME:
        if (n->sym->type.len) {
            I("lda #<%s", n->sym->label);
            if (w == 2)
                I("ldx #>%s", n->sym->label);
            return;
        }
        I("lda %s", n->sym->label);
        if (w == 2) {
            if (width(n->type) == 2)
                I("ldx %s+1", n->sym->label);
            else
                convert(g, 1, signed_type(n->type), 2);
        }
        return;
    case N_UNARY:
        if (n->op == OP_NOT) {
            truth(g, n, w);
            return;
        }
        gen(g, n->a, w);
        I("eor #$FF");
        if (n->op == OP_NEG) {
            I("clc");
            I("adc #1");
        }
        if (w == 2) {
            I("tay");
            I("txa");
            I("eor #$FF");
            if (n->op == OP_NEG)
                I("adc #0");
            I("tax");
            I("tya");
        }
        return;
    case N_BINARY:
        if (n->op >= OP_EQ && n->op <= OP_GE)
            truth(g, n, w);
        else
            arithmetic(g, n, w);
        return;
    case N_AND:
    case N_OR:
        truth(g, n, w);
        return;
    case N_COND: {
        const int other = new_label(g), end = new_label(g);
        cond(g, n->a, other, 0);
        gen(g, n->b, w);
        I("jmp L%d", end);
        label(g, other);
        gen(g, n->c, w);
        label(g, end);
        return;
    }
    case N_ASSIGN:
        assign(g, n, 1, w);
        return;
    case N_PREINC:
    case N_POSTINC:
        step(g, n, 1, w);
        return;
    case N_INDEX:
    case N_DEREF:
        load(g, n);
        convert(g, width(n->type), signed_type(n->type), w);
        return;
    case N_ADDR:
        if (n->a->kind == N_NAME && n->a->sym->type.len)
            gen(g, n->a, 2);
        else
            address(g, n->a);
        return;
    case N_CAST: {
        const int tw = width(n->type);
        gen(g, n->a, tw);
        convert(g, tw, signed_type(n->type), w);
        return;
    }
    case N_CALL:
        call(g, n);
        convert(g, width(n->type), signed_type(n->type), w);
        return;
    default:
        cc_error(g->cc, n->line, "this cannot be compiled");
    }
}

/* An expression whose value is not used. */
static void effect(gen_t *g, node_t *n)
{
    if (n->kind == N_ASSIGN)
        assign(g, n, 0, width(n->a->type));
    else if (n->kind == N_PREINC || n->kind == N_POSTINC)
        step(g, n, 0, width(n->a->type));
    else if (n->kind == N_CALL && !n->constant)
        call(g, n);
    else if (!n->constant)
        gen(g, n, width(n->type));
}

static void inline_asm(gen_t *g, const node_t *n)
{
    for (const node_t *s = n->a; s; s = s->next) {
        g->line = s->line;
        char line[240];
        size_t k = 0;
        for (const char *p = s->name;; p++) {
            if (*p == '{') {
                const char *end = strchr(p, '}');
                char name[48];
                memcpy(name, p + 1, (size_t)(end - p - 1));
                name[end - p - 1] = 0;
                const sym_t *v = find(g, name);
                if (v && v->kind == SY_CONST)
                    k += (size_t)snprintf(line + k, sizeof line - k, "%ld", v->value);
                else if (v)
                    k += (size_t)snprintf(line + k, sizeof line - k, "%s", v->label);
                if (k >= sizeof line - 1)
                    k = sizeof line - 1;
                p = end;
            } else if (!*p || *p == '\n') {
                line[k] = 0;
                const int label_line = k && line[k - 1] == ':';
                emit(g, "%s%s", label_line ? "" : "    ", line);
                k = 0;
                if (!*p)
                    break;
            } else if (k < sizeof line - 1)
                line[k++] = *p;
        }
    }
}

/* The values of a local variable, when its declaration is reached. */
static void initialise(gen_t *g, const node_t *d)
{
    const sym_t *s = d->sym;
    if (!s || s->kind != SY_VAR || !d->a)
        return;
    if (d->a->kind != N_LIST) {
        gen(g, d->a, width(s->type));
        I("sta %s", s->label);
        if (width(s->type) == 2)
            I("stx %s+1", s->label);
        return;
    }
    const int size = element(s->type);
    if (s->type.len * size > 256)
        cc_error(g->cc, d->line, "'%s' has a list for more than 256 bytes: fill it with a loop", s->name);
    node_t *v = d->a->a;
    for (int i = 0; i < s->type.len; i++, v = v ? v->next : NULL) {
        if (v)
            gen(g, v, size);
        else
            immediate(g, 0, size);
        I("sta %s+%d", s->label, i * size);
        if (size == 2)
            I("stx %s+%d", s->label, i * size + 1);
    }
}

static void statement(gen_t *g, node_t *n)
{
    if (n->kind != S_BLOCK && n->kind != S_EMPTY)
        comment(g, n->line);
    g->line = n->line;
    const int brk = g->brk, cont = g->cont;
    switch (n->kind) {
    case S_BLOCK:
        for (node_t *s = n->a; s; s = s->next)
            statement(g, s);
        break;
    case S_DECL:
        initialise(g, n);
        break;
    case S_IF: {
        const int other = new_label(g), end = new_label(g);
        cond(g, n->a, other, 0);
        statement(g, n->b);
        if (n->c) {
            I("jmp L%d", end);
            label(g, other);
            statement(g, n->c);
            label(g, end);
        } else
            label(g, other);
        break;
    }
    case S_WHILE: {
        const int top = new_label(g), end = new_label(g);
        g->cont = top;
        g->brk = end;
        label(g, top);
        cond(g, n->a, end, 0);
        statement(g, n->b);
        I("jmp L%d", top);
        label(g, end);
        break;
    }
    case S_DO: {
        const int top = new_label(g), next = new_label(g), end = new_label(g);
        g->cont = next;
        g->brk = end;
        label(g, top);
        statement(g, n->b);
        label(g, next);
        g->line = n->a->line;
        cond(g, n->a, top, 1);
        label(g, end);
        break;
    }
    case S_FOR: {
        const int top = new_label(g), next = new_label(g), end = new_label(g);
        for (node_t *s = n->a; s; s = s->next)
            statement(g, s);
        g->cont = next;
        g->brk = end;
        label(g, top);
        if (n->b)
            cond(g, n->b, end, 0);
        statement(g, n->d);
        label(g, next);
        g->line = n->line;
        if (n->c)
            effect(g, n->c);
        I("jmp L%d", top);
        label(g, end);
        break;
    }
    case S_BREAK:
        I("jmp L%d", g->brk);
        break;
    case S_CONTINUE:
        I("jmp L%d", g->cont);
        break;
    case S_RETURN:
        if (n->a)
            gen(g, n->a, width(g->fn->type));
        I("rts");
        break;
    case S_EXPR:
        effect(g, n->a);
        break;
    case S_ASM:
        inline_asm(g, n);
        break;
    default:
        break;
    }
    g->brk = brk;
    g->cont = cont;
}

/* asm("...{x}...") needs the names of its scope again while it is written. */
static void open_names(gen_t *g, node_t *n)
{
    for (; n; n = n->next) {
        if (n->kind == S_DECL && n->sym) {
            sym_t *s = n->sym;
            if (!find_in(g->locals, s->name)) {
                sym_t *copy = cc_alloc(g->cc, sizeof *copy);
                *copy = *s;
                copy->next = g->locals;
                g->locals = copy;
            }
        }
        if (n->kind == S_BLOCK)
            open_names(g, n->a);
        if (n->kind >= S_IF && n->kind <= S_FOR) {
            if (n->kind == S_IF) {
                open_names(g, n->b);
                open_names(g, n->c);
            } else if (n->kind == S_FOR) {
                open_names(g, n->a);
                open_names(g, n->d);
            } else
                open_names(g, n->b);
        }
    }
}

static void data_lines(gen_t *g, const sym_t *s, const char *name)
{
    const int size = element(s->type);
    emit(g, "%s:", name);
    const node_t *v = s->init && s->init->kind == N_LIST ? s->init->a : s->init;
    const int count = s->type.len ? s->type.len : 1;
    for (int i = 0; i < count;) {
        char line[200];
        size_t k = (size_t)snprintf(line, sizeof line, "    .%s ", size == 2 ? "word" : "byte");
        for (int j = 0; j < 16 && i < count; j++, i++, v = v ? v->next : NULL)
            k += (size_t)snprintf(line + k, sizeof line - k, "%s$%0*X", j ? "," : "", 2 * size,
                                  (unsigned)((v ? v->value : 0) & (size == 2 ? 0xFFFF : 0xFF)));
        emit(g, "%s", line);
    }
}

/* The sheet: NES, the CHR ROM; SNES, bytes of the ROM that the start sends to the video memory. */
static void tiles(gen_t *g)
{
    const int bytes = g->snes ? 32 : 16;
    if (!g->prog.tiles)
        return;
    g->line = g->prog.tiles->line;
    if (g->snes) {
        emit(g, "; ---- the tiles: 32 bytes a tile, sent to the video memory at the start -----");
        emit(g, "__tiles:");
    } else {
        emit(g, "; ---- the tiles: the CHR ROM, 16 bytes a tile --------------------------------");
        emit(g, ".chr");
    }
    long total = 0;
    char line[200];
    size_t k = 0;
    int n = 0;
    for (const node_t *v = g->prog.tiles->a;; v = v->next) {
        if (n && (!v || v->kind == N_BLANK || n == 16)) {
            emit(g, "%s", line);
            n = 0;
        }
        if (!v)
            break;
        g->line = v->line;
        if (v->kind == N_BLANK) {
            if (v->value < 0 || v->value > 512)
                cc_error(g->cc, v->line, "blank(n): n tiles, 0 to 512");
            if (total % bytes)
                cc_error(g->cc, v->line, "blank() starts after %ld bytes: each tile before it needs %d", total, bytes);
            if (v->value)
                emit(g, "    .res %ld,0", v->value * bytes);
            total += v->value * bytes;
        } else {
            if (v->value < 0 || v->value > 255)
                cc_error(g->cc, v->line, "a byte of a tile is 0 to 255, not %ld", v->value);
            if (!n)
                k = (size_t)snprintf(line, sizeof line, "    .db $%02X", (unsigned)v->value);
            else
                k += (size_t)snprintf(line + k, sizeof line - k, ",$%02X", (unsigned)v->value);
            n++;
            total++;
        }
        if (total > 65536)
            cc_error(g->cc, v->line, "too many tiles");
    }
    if (total % bytes)
        cc_error(g->cc, g->prog.tiles->line, "tiles has %ld bytes: a tile is %d", total, bytes);
    if (g->snes && total > 512 * 32)
        cc_error(g->cc, g->prog.tiles->line, "the sprites reach 512 tiles: tiles has %ld", total / 32);
    if (!g->snes)
        emit(g, ".endchr");
}

/* The bytes that the tiles { } block gives. */
static long tile_bytes(const gen_t *g)
{
    long total = 0;
    for (const node_t *v = g->prog.tiles ? g->prog.tiles->a : NULL; v; v = v->next)
        total += v->kind == N_BLANK ? v->value * (g->snes ? 32 : 16) : 1;
    return total;
}

/* SNES: the routine of the start that copies the tiles to the video memory at $0000. */
static void upload_tiles(gen_t *g)
{
    const long total = tile_bytes(g);
    g->line = 0;
    emit(g, "%s", "");
    emit(g, "__upload_tiles:");
    if (total > 0 && total <= 512 * 32) {
        static const char *const code[] = { "lda #$80", "sta $2115", "stz $2116", "stz $2117", "lda #$01",
            "sta $4300", "lda #$18", "sta $4301", "lda #<__tiles", "sta $4302", "lda #>__tiles", "sta $4303",
            "stz $4304" };
        for (size_t i = 0; i < sizeof code / sizeof code[0]; i++)
            I("%s", code[i]);
        I("lda #$%02X", (unsigned)(total & 0xFF));
        I("sta $4305");
        I("lda #$%02X", (unsigned)(total >> 8 & 0xFF));
        I("sta $4306");
        I("lda #1");
        I("sta $420B");
    }
    I("rts");
}

/* The start and the vertical blank: with sound, the NES calls the driver in them. */
static void base_runtime(gen_t *g)
{
    const char *at = cc_runtime_part(g->snes, RT_BASE);
    static const char *const hooks[2][2] = { { "@@snd_init\n", "    jsr snd_init\n" },
                                             { "@@snd_frame\n", "    jsr snd_frame\n" } };
    for (int i = 0; i < 2; i++) {
        const char *mark = strstr(at, hooks[i][0]);
        if (!mark)
            break;
        put(g, at, (size_t)(mark - at));
        if (g->sound)
            put(g, hooks[i][1], strlen(hooks[i][1]));
        at = mark + strlen(hooks[i][0]);
    }
    put(g, at, strlen(at));
}

static void write_program(gen_t *g)
{
    const int ram = (g->zp - CC_ZP_FIRST) + (g->ram - g->ram_first);
    g->line = 0;
    emit(g, "; Written by bm C from main.c. Do not edit: change main.c.");
    emit(g, "; RAM of the program: %d bytes (%d in the zero page, %d from $%04X).", ram, g->zp - CC_ZP_FIRST,
         g->ram - g->ram_first, (unsigned)g->ram_first);
    emit(g, "; A byte is in A; 16 bits are in A (low) and X (high).");
    emit(g, "%s", "");
    put(g, cc_runtime_head(g->snes), strlen(cc_runtime_head(g->snes)));
    emit(g, "%s", "");
    emit(g, "; ---- the variables of the program -------------------------------------------");
    for (const sym_t *s = g->all; s; s = s->all)
        if (s->kind == SY_VAR) {
            g->line = s->line;
            emit(g, "%-22s = $%04X   ; %s%s, line %d", s->label, (unsigned)s->address, s->name,
                 s->param ? " (parameter)" : s->type.len ? "[]" : "", s->line);
        }
    g->line = 0;
    emit(g, "%s", "");
    emit(g, ".org $8000");
    emit(g, "%s", "");
    base_runtime(g);
    if (g->snes)
        upload_tiles(g);

    emit(g, "%s", "");
    emit(g, "; ---- the program ------------------------------------------------------------");
    const int has_start = find_in(g->globals, "start") != NULL, has_main = find_in(g->globals, "main") != NULL;
    emit(g, "__program:");
    I("jsr __init");
    if (has_start)
        I("jsr fn_start");
    if (has_main) {
        I("jsr fn_main");
        emit(g, "__end:");
        I("jmp __end");
    } else {
        emit(g, "__loop:                    ; once every frame");
        I("jsr __wait_frame");
        I("jsr fn_update");
        I("jmp __loop");
    }

    emit(g, "__init:                    ; the first values of the globals");
    for (node_t *d = g->prog.decls; d; d = d->next) {
        const sym_t *s = d->kind == S_DECL ? d->sym : NULL;
        if (!s || s->kind != SY_VAR || !d->a)
            continue;
        comment(g, d->line);
        if (d->a->kind != N_LIST)
            initialise(g, d);
        else {
            const int n = bytes_of(s), l = new_label(g);
            I("ldx #0");
            label(g, l);
            I("lda di_%s,x", s->name);
            I("sta %s,x", s->label);
            I("inx");
            I("cpx #$%02X", (unsigned)(n & 0xFF));
            I("bne L%d", l);
        }
    }
    I("rts");

    for (node_t *d = g->prog.decls; d; d = d->next) {
        if (d->kind != S_BLOCK)
            continue;
        g->fn = d->sym;
        g->locals = NULL;
        for (int i = 0; i < g->fn->nparams; i++) {
            sym_t *copy = cc_alloc(g->cc, sizeof *copy);
            *copy = *g->fn->params[i];
            copy->next = g->locals;
            g->locals = copy;
        }
        open_names(g, d->a);
        emit(g, "%s", "");
        g->line = d->line;
        g->commented = 0;
        comment(g, d->line);
        emit(g, "%s:", g->fn->label);
        g->line = d->line;
        statement(g, d->a);
        g->line = d->line;
        I("rts");
        g->locals = NULL;
        g->fn = NULL;
    }

    g->line = 0;
    for (int part = RT_MUL; part <= RT_RANDOM; part <<= 1)
        if (g->parts & part) {
            emit(g, "%s", "");
            put(g, cc_runtime_part(g->snes, part), strlen(cc_runtime_part(g->snes, part)));
        }

    if (g->sound) {
        emit(g, "%s", "");
        put(g, g->sound, strlen(g->sound));
    }

    int first = 1;
    for (const sym_t *s = g->all; s; s = s->all) {
        const int copy = s->kind == SY_VAR && s->init && s->init->kind == N_LIST && !strncmp(s->label, "v_", 2);
        if (s->kind != SY_ROM && !copy)
            continue;
        if (first)
            emit(g, "\n; ---- data in the ROM --------------------------------------------------------");
        first = 0;
        g->line = s->line;
        if (s->kind == SY_ROM)
            data_lines(g, s, s->label);
        else {
            char name[128];
            snprintf(name, sizeof name, "di_%s", s->name);
            data_lines(g, s, name);
        }
    }
    emit(g, "%s", "");
    tiles(g);
}

static void builtin_names(gen_t *g)
{
    const constant_t *c = g->snes ? snes_constants : nes_constants;
    const size_t count = g->snes ? sizeof snes_constants / sizeof snes_constants[0] :
                                   sizeof nes_constants / sizeof nes_constants[0];
    for (size_t i = 0; i < count; i++) {
        sym_t *s = new_sym(g, c[i].name, SY_CONST, 0);
        s->value = c[i].value;
        declare_global(g, s);
    }
    for (int i = 0; i < B_COUNT; i++) {
        sym_t *s = new_sym(g, builtins[i].name, SY_BUILTIN, 0);
        s->builtin = i;
        declare_global(g, s);
    }
    static const char *const vars[3][2] = { { "frame", "__frame" }, { "pad", "__pad" }, { "pad_new", "__new" } };
    for (int i = 0; i < 3; i++) {
        sym_t *s = new_sym(g, vars[i][0], SY_VAR, 0);
        s->type = (ctype_t){ (uint8_t)(g->snes && i ? CT_U16 : CT_U8), 0, 0 };     /* the SNES pad: a word */
        s->is_const = 1;                        /* the runtime writes them */
        snprintf(s->label, sizeof s->label, "%s", vars[i][1]);
        declare_global(g, s);
    }
}

/* The sound of a NES program: reads the music block, gives its numbers their names, and hands the reader a source
 * where the block is empty lines. Returns the source to read (from malloc), or NULL with the message in err. */
static char *sound(gen_t *g, const char *src, size_t len, sdk_music_t *m)
{
    char why[128];
    if (!g->snes && sdk_music_parse(src, len, 1, m, why, sizeof why) != 0) {
        int line = 1, used = 0;
        if (sscanf(why, "line %d: %n", &line, &used) != 1)
            used = 0;
        snprintf(g->cc->err, g->cc->en, "main.c:%d: music: %s", line, why + used);
        return NULL;
    }
    char *copy = malloc(len + 1);
    if (!copy) {
        snprintf(g->cc->err, g->cc->en, "main.c:1: not enough memory to compile");
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = 0;
    size_t from, to;
    if (!g->snes && sdk_music_find(src, len, 1, &from, &to) > 0)
        for (size_t i = from; i < to; i++)
            if (copy[i] != '\n')
                copy[i] = ' ';
    if (!g->snes && (!sdk_music_empty(m) || sdk_music_called(copy, len, 1))) {
        g->sound = sdk_music_asm(m);
        if (!g->sound) {
            snprintf(g->cc->err, g->cc->en, "main.c:1: not enough memory for the music");
            free(copy);
            return NULL;
        }
        g->zp_end = 0xFC;                       /* the driver's places */
        g->ram_end = 0x700;
    }
    return copy;
}

/* song_NAME and sfx_NAME: the numbers that play_song and play_sfx take. */
static void sound_names(gen_t *g, const sdk_music_t *m)
{
    for (int i = 0; i < m->songs + m->sfxs; i++) {
        const int song = i < m->songs;
        char name[24];
        snprintf(name, sizeof name, "%s_%s", song ? "song" : "sfx",
                 song ? m->song[i].name : m->sfx[i - m->songs].name);
        sym_t *s = new_sym(g, cc_strdup(g->cc, name, strlen(name)), SY_CONST, 0);
        s->value = song ? i : i - m->songs;
        declare_global(g, s);
    }
}

int sdk_cc_compile(const char *src, size_t len, int snes, sdk_cc_out_t *out, char *err, size_t en)
{
    cc_t *cc = calloc(1, sizeof *cc);
    gen_t *g = calloc(1, sizeof *g);
    sdk_music_t *music = calloc(1, sizeof *music);
    char *volatile read = NULL;                 /* volatile: set after setjmp could come back */
    memset(out, 0, sizeof *out);
    int rc = -1;
    if (!cc || !g || !music) {
        snprintf(err, en, "main.c:1: not enough memory to compile");
        goto done;
    }
    cc->src = src;
    cc->len = len;
    cc->err = err;
    cc->en = en;
    cc->out = out;
    cc->user = g;
    g->cc = cc;
    g->all_tail = &g->all;
    g->snes = snes != 0;
    g->ram_first = snes ? CC_SNES_FIRST : CC_RAM_FIRST;
    g->ram_end = snes ? CC_SNES_END : CC_RAM_END;
    g->zp = CC_ZP_FIRST;
    g->zp_end = CC_ZP_END;
    g->ram = g->ram_first;
    read = sound(g, src, len, music);
    if (!read)
        goto done;
    if (sdk_anim_called(read, len, 1)) {        /* animate(): its code after the program's lines */
        const char *more = sdk_anim_prelude(g->snes);
        const size_t n = strlen(more);
        char *with = malloc(len + n + 2);
        if (!with) {
            snprintf(err, en, "main.c:1: not enough memory to compile");
            goto done;
        }
        memcpy(with, read, len);
        with[len] = '\n';
        memcpy(with + len + 1, more, n + 1);
        free(read);
        read = with;
        cc->len = len + 1 + n;
    }
    cc->src = read;
    if (!setjmp(cc->fail)) {
        builtin_names(g);
        sound_names(g, music);
        cc_parse(cc, &g->prog, define);
        analyse(g);
        write_program(g);
        out->text = g->text;
        out->len = g->len;
        out->line_of = g->line_of;
        out->lines = g->lines;
        out->ram = (g->zp - CC_ZP_FIRST) + (g->ram - g->ram_first);
        out->ram_size = g->snes ? 8192 : 2048;
        g->text = NULL;
        g->line_of = NULL;
        rc = 0;
    }
done:
    if (cc)
        for (header_t *h = cc->blocks, *next; h; h = next) {
            next = h->next;
            free(h);
        }
    if (g) {
        free(g->text);
        free(g->line_of);
        free(g->sound);
    }
    free(music);
    free(read);
    free(g);
    free(cc);
    return rc;
}

void sdk_cc_free(sdk_cc_out_t *out)
{
    free(out->text);
    free(out->line_of);
    memset(out, 0, sizeof *out);
}
