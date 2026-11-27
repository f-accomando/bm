/*
 * Shell SDK, bm C: the reader, from the text to the tree (see shell_sdk_cc_int.h). Original code.
 */
#include "shell_sdk_cc_int.h"

#include <stdio.h>
#include <string.h>

enum { T_END, T_NUM, T_NAME, T_STRING, T_PUNCT };

typedef struct {
    int kind;
    int line;
    long value;
    const char *text;           /* of the source; of a string, its characters */
    int len;
    int line_end;               /* nothing else on its line after it (for #define) */
} token_t;

typedef struct {
    cc_t *cc;
    size_t pos;
    int line;
    int depth;
    token_t tok, prev;
    void (*define)(cc_t *, const char *, node_t *, int);
} parser_t;

/* ---- the words ---- */

static int is_name(int c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int digit_value(int c)
{
    return is_digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
}

static int peek_char(const parser_t *p, size_t at) { return at < p->cc->len ? (unsigned char)p->cc->src[at] : 0; }

/* Spaces and comments. */
static void skip_blank(parser_t *p)
{
    for (;;) {
        const int c = peek_char(p, p->pos);
        if (c == '\n') {
            p->line++;
            p->pos++;
        } else if (c == ' ' || c == '\t' || c == '\r')
            p->pos++;
        else if (c == '/' && peek_char(p, p->pos + 1) == '/')
            while (p->pos < p->cc->len && peek_char(p, p->pos) != '\n')
                p->pos++;
        else if (c == '/' && peek_char(p, p->pos + 1) == '*') {
            const int from = p->line;
            for (p->pos += 2; p->pos < p->cc->len; p->pos++) {
                if (peek_char(p, p->pos) == '*' && peek_char(p, p->pos + 1) == '/')
                    break;
                p->line += peek_char(p, p->pos) == '\n';
            }
            if (p->pos >= p->cc->len)
                cc_error(p->cc, from, "this comment is never closed: add */");
            p->pos += 2;
        } else
            return;
    }
}

static int escape(parser_t *p, int c)
{
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case '0': return 0;
    case '\\': case '\'': case '"': return c;
    default: cc_error(p->cc, p->line, "\\%c is not an escape bm C knows (\\n \\t \\0 \\\\ \\\" \\')", c);
    }
}

static void next(parser_t *p)
{
    static const char *const long_punct[] = { "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "++",
                                              "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "->" };
    p->prev = p->tok;
    skip_blank(p);
    token_t *t = &p->tok;
    memset(t, 0, sizeof *t);
    t->line = p->line;
    t->text = p->cc->src + p->pos;
    if (p->pos >= p->cc->len)
        return;
    const int c = peek_char(p, p->pos);
    if (is_name(c)) {
        t->kind = T_NAME;
        while (is_name(peek_char(p, p->pos)) || is_digit(peek_char(p, p->pos)))
            p->pos++, t->len++;
    } else if (is_digit(c)) {
        int base = 10;
        if (c == '0' && (peek_char(p, p->pos + 1) == 'x' || peek_char(p, p->pos + 1) == 'X'))
            base = 16, p->pos += 2;
        else if (c == '0' && (peek_char(p, p->pos + 1) == 'b' || peek_char(p, p->pos + 1) == 'B'))
            base = 2, p->pos += 2;
        t->kind = T_NUM;
        int digits = 0;
        for (; digit_value(peek_char(p, p->pos)) < base; p->pos++, digits++) {
            t->value = t->value * base + digit_value(peek_char(p, p->pos));
            if (t->value > 65535)
                cc_error(p->cc, p->line, "the largest number is 65535 (a word)");
        }
        if (!digits)
            cc_error(p->cc, p->line, "a number needs digits");
        while (peek_char(p, p->pos) == 'u' || peek_char(p, p->pos) == 'U')
            p->pos++;
        if (is_name(peek_char(p, p->pos)) || is_digit(peek_char(p, p->pos)) || peek_char(p, p->pos) == '.')
            cc_error(p->cc, p->line, "this number is not whole: bm C has no fractions and no long numbers");
        t->len = (int)(p->cc->src + p->pos - t->text);
    } else if (c == '\'') {
        p->pos++;
        int v = peek_char(p, p->pos++);
        if (v == '\\')
            v = escape(p, peek_char(p, p->pos++));
        if (peek_char(p, p->pos++) != '\'')
            cc_error(p->cc, p->line, "a character is one letter between quotes: 'a'");
        t->kind = T_NUM;
        t->value = v;
        t->len = (int)(p->cc->src + p->pos - t->text);
    } else if (c == '"') {
        char *s = cc_alloc(p->cc, 256);
        int n = 0;
        for (p->pos++; peek_char(p, p->pos) != '"'; ) {
            int v = peek_char(p, p->pos++);
            if (p->pos > p->cc->len || v == '\n')
                cc_error(p->cc, t->line, "this string is never closed: add \"");
            if (v == '\\')
                v = escape(p, peek_char(p, p->pos++));
            if (n >= 255)
                cc_error(p->cc, t->line, "a string holds 255 characters at most");
            s[n++] = (char)v;
        }
        p->pos++;
        t->kind = T_STRING;
        t->text = s;
        t->len = n;
    } else {
        t->kind = T_PUNCT;
        for (size_t i = 0; i < sizeof long_punct / sizeof long_punct[0]; i++) {
            const size_t n = strlen(long_punct[i]);
            if (p->cc->len - p->pos >= n && !memcmp(p->cc->src + p->pos, long_punct[i], n)) {
                p->pos += n;
                t->len = (int)n;
                return;
            }
        }
        if (!strchr("+-*/%&|^~!<>=(){}[];,?:.#", c))
            cc_error(p->cc, p->line, "the character '%c' means nothing in bm C", c >= 32 && c < 127 ? c : '?');
        p->pos++;
        t->len = 1;
    }
}

static int is(const parser_t *p, const char *s)
{
    return (p->tok.kind == T_PUNCT || p->tok.kind == T_NAME) && p->tok.len == (int)strlen(s) &&
           !memcmp(p->tok.text, s, (size_t)p->tok.len);
}

static int accept(parser_t *p, const char *s)
{
    if (!is(p, s))
        return 0;
    next(p);
    return 1;
}

/* The word on the screen, for a message. */
static const char *seen(parser_t *p)
{
    if (p->tok.kind == T_END)
        return "the end of the file";
    if (p->tok.kind == T_STRING)
        return "a string";
    char *s = cc_alloc(p->cc, 64);
    snprintf(s, 64, "'%.*s'", p->tok.len > 40 ? 40 : p->tok.len, p->tok.text);
    return s;
}

static void expect(parser_t *p, const char *s, const char *why)
{
    if (accept(p, s))
        return;
    if (!strcmp(s, ";"))                        /* the mistake is where the statement ended */
        cc_error(p->cc, p->prev.line, "add ';' %s", why);
    cc_error(p->cc, p->tok.line, "expected '%s' %s, found %s", s, why, seen(p));
}

static node_t *node(parser_t *p, int kind, int line)
{
    node_t *n = cc_alloc(p->cc, sizeof *n);
    n->kind = (uint8_t)kind;
    n->line = line;
    return n;
}

static const char *name_here(parser_t *p, const char *what)
{
    static const char *const reserved[] = { "if", "else", "while", "do", "for", "break", "continue", "return",
        "void", "byte", "sbyte", "word", "int", "char", "bool", "unsigned", "signed", "short", "const", "static",
        "asm", "tiles" };
    if (p->tok.kind != T_NAME)
        cc_error(p->cc, p->tok.line, "expected the name of %s, found %s", what, seen(p));
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; i++)
        if (is(p, reserved[i]))
            cc_error(p->cc, p->tok.line, "'%s' is a word of the language: choose another name for %s", reserved[i],
                     what);
    if (p->tok.len > 40)
        cc_error(p->cc, p->tok.line, "a name holds 40 characters at most");
    const char *s = cc_strdup(p->cc, p->tok.text, (size_t)p->tok.len);
    next(p);
    return s;
}

/* Words of C that bm C does not have: the message says what to write. */
static void refuse(parser_t *p)
{
    static const char *const no[][2] = {
        { "float", "the NES has no fractions: use word, and count in tenths or in pixels times 16" },
        { "double", "the NES has no fractions: use word, and count in tenths or in pixels times 16" },
        { "long", "the largest type is word (16 bits)" },
        { "struct", "bm C has no struct yet: use one array for each field (enemy_x[], enemy_y[])" },
        { "union", "bm C has no union" },
        { "enum", "bm C has no enum yet: write #define NAME number" },
        { "typedef", "bm C has no typedef: use byte, sbyte, word and int" },
        { "switch", "bm C has no switch yet: write if, else if" },
        { "case", "bm C has no switch yet: write if, else if" },
        { "goto", "bm C has no goto: use while, break and continue" },
        { "sizeof", "bm C has no sizeof: give the size a name with #define" } };
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++)
        if (is(p, no[i][0]))
            cc_error(p->cc, p->tok.line, "%s: %s", no[i][0], no[i][1]);
}

/* ---- types ---- */

static int starts_type(parser_t *p)
{
    static const char *const words[] = { "void", "byte", "sbyte", "word", "int", "char", "bool", "unsigned",
                                         "signed", "short", "const", "static" };
    refuse(p);
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
        if (is(p, words[i]))
            return 1;
    return 0;
}

/* const unsigned char, byte, int*, ... The stars belong to each name: read them with stars(). */
static ctype_t base_type(parser_t *p, int *is_const)
{
    ctype_t t = { CT_VOID, 0, 0 };
    int sign = 0, set = 0, wide = 0;
    *is_const = 0;
    for (;;) {
        refuse(p);
        if (accept(p, "const"))
            *is_const = 1;
        else if (accept(p, "static"))
            ;                                       /* every variable has a fixed place already */
        else if (accept(p, "unsigned"))
            sign = 1;
        else if (accept(p, "signed"))
            sign = -1;
        else if (accept(p, "short"))
            wide = 1;
        else if (!set && accept(p, "void"))
            set = 1, t.base = CT_VOID;
        else if (!set && (accept(p, "byte") || accept(p, "bool")))
            set = 1, t.base = CT_U8;
        else if (!set && accept(p, "sbyte"))
            set = 1, t.base = CT_S8;
        else if (!set && accept(p, "word"))
            set = 1, t.base = CT_U16;
        else if (!set && accept(p, "char"))
            set = 1, t.base = sign < 0 ? CT_S8 : CT_U8;
        else if (!set && accept(p, "int"))
            set = 1, t.base = sign > 0 ? CT_U16 : CT_S16;
        else
            break;
    }
    if (!set) {
        if (!sign && !wide)
            cc_error(p->cc, p->tok.line, "expected a type (byte, word, int, sbyte, void), found %s", seen(p));
        t.base = sign > 0 ? CT_U16 : CT_S16;        /* "unsigned x", "short x" */
    }
    return t;
}

static ctype_t stars(parser_t *p, ctype_t t)
{
    while (accept(p, "*"))
        if (++t.ptr > 2)
            cc_error(p->cc, p->tok.line, "two '*' at most");
    return t;
}

/* ---- expressions ---- */

static node_t *expression(parser_t *p);
static node_t *assignment(parser_t *p);

static void deeper(parser_t *p)
{
    if (++p->depth > CC_DEPTH)
        cc_error(p->cc, p->tok.line, "this is nested too deep: split it with a variable or a function");
}

static node_t *primary(parser_t *p)
{
    const int line = p->tok.line;
    refuse(p);
    if (p->tok.kind == T_NUM) {
        node_t *n = node(p, N_NUM, line);
        n->value = p->tok.value;
        n->constant = 1;
        next(p);
        return n;
    }
    if (p->tok.kind == T_STRING)
        cc_error(p->cc, line, "bm C has strings only inside asm(\"...\")");
    if (accept(p, "true") || accept(p, "false")) {
        node_t *n = node(p, N_NUM, line);
        n->value = p->prev.len == 4;
        n->constant = 1;
        return n;
    }
    if (accept(p, "(")) {
        node_t *n = expression(p);
        expect(p, ")", "to close the '('");
        n->wrapped = 1;
        return n;
    }
    if (p->tok.kind != T_NAME)
        cc_error(p->cc, line, "expected a value, found %s", seen(p));
    node_t *n = node(p, N_NAME, line);
    n->name = name_here(p, "a value");
    return n;
}

static node_t *postfix(parser_t *p)
{
    node_t *n = primary(p);
    for (;;) {
        const int line = p->tok.line;
        if (accept(p, "[")) {
            node_t *m = node(p, N_INDEX, line);
            m->a = n;
            m->b = expression(p);
            expect(p, "]", "after the index");
            n = m;
        } else if (accept(p, "(")) {
            if (n->kind != N_NAME)
                cc_error(p->cc, line, "only a function can be called");
            node_t *m = node(p, N_CALL, line);
            m->name = n->name;
            node_t **tail = &m->a;
            int count = 0;
            while (!accept(p, ")")) {
                if (count++)
                    expect(p, ",", "between the arguments");
                if (count > CC_PARAMS)
                    cc_error(p->cc, line, "a function takes %d arguments at most", CC_PARAMS);
                *tail = assignment(p);
                tail = &(*tail)->next;
            }
            n = m;
        } else if (is(p, "++") || is(p, "--")) {
            node_t *m = node(p, N_POSTINC, line);
            m->op = is(p, "++") ? OP_ADD : OP_SUB;
            m->a = n;
            next(p);
            n = m;
        } else if (is(p, ".") || is(p, "->"))
            cc_error(p->cc, line, "bm C has no struct yet: use one array for each field");
        else
            return n;
    }
}

static node_t *unary(parser_t *p)
{
    const int line = p->tok.line;
    node_t *n;
    deeper(p);
    if (is(p, "++") || is(p, "--")) {
        n = node(p, N_PREINC, line);
        n->op = is(p, "++") ? OP_ADD : OP_SUB;
        next(p);
        n->a = unary(p);
    } else if (is(p, "-") || is(p, "!") || is(p, "~")) {
        n = node(p, N_UNARY, line);
        n->op = is(p, "-") ? OP_NEG : is(p, "!") ? OP_NOT : OP_INV;
        next(p);
        n->a = unary(p);
    } else if (accept(p, "+"))
        n = unary(p);
    else if (accept(p, "*")) {
        n = node(p, N_DEREF, line);
        n->a = unary(p);
    } else if (accept(p, "&")) {
        n = node(p, N_ADDR, line);
        n->a = unary(p);
    } else if (is(p, "(")) {
        const parser_t back = *p;
        next(p);
        if (starts_type(p)) {                       /* a cast */
            int is_const;
            n = node(p, N_CAST, line);
            n->type = stars(p, base_type(p, &is_const));
            expect(p, ")", "after the type of the cast");
            n->a = unary(p);
        } else {
            *p = back;
            n = postfix(p);
        }
    } else
        n = postfix(p);
    p->depth--;
    return n;
}

static const struct { const char *text; int op, level; } binaries[] = {
    { "||", OP_OR, 1 }, { "&&", OP_AND, 2 }, { "|", OP_OR, 3 }, { "^", OP_XOR, 4 }, { "&", OP_AND, 5 },
    { "==", OP_EQ, 6 }, { "!=", OP_NE, 6 }, { "<", OP_LT, 7 }, { ">", OP_GT, 7 }, { "<=", OP_LE, 7 },
    { ">=", OP_GE, 7 }, { "<<", OP_SHL, 8 }, { ">>", OP_SHR, 8 }, { "+", OP_ADD, 9 }, { "-", OP_SUB, 9 },
    { "*", OP_MUL, 10 }, { "/", OP_DIV, 10 }, { "%", OP_MOD, 10 } };

static node_t *binary(parser_t *p, int level)
{
    node_t *n = unary(p);
    const int depth = p->depth;
    for (;;) {
        int i = 0;
        const int count = (int)(sizeof binaries / sizeof binaries[0]);
        while (i < count && !is(p, binaries[i].text))
            i++;
        if (i == count || binaries[i].level < level)
            break;
        deeper(p);                                  /* a long chain is a deep tree */
        const int kind = binaries[i].level == 1 ? N_OR : binaries[i].level == 2 ? N_AND : N_BINARY;
        node_t *m = node(p, kind, p->tok.line);
        m->op = (uint8_t)binaries[i].op;
        next(p);
        m->a = n;
        m->b = binary(p, binaries[i].level + 1);
        n = m;
    }
    p->depth = depth;
    return n;
}

static node_t *conditional(parser_t *p)
{
    node_t *n = binary(p, 1);
    if (!is(p, "?"))
        return n;
    node_t *m = node(p, N_COND, p->tok.line);
    next(p);
    deeper(p);
    m->a = n;
    m->b = expression(p);
    expect(p, ":", "between the two values of '?'");
    m->c = conditional(p);
    p->depth--;
    return m;
}

static node_t *assignment(parser_t *p)
{
    static const struct { const char *text; int op; } set[] = {
        { "=", OP_NONE }, { "+=", OP_ADD }, { "-=", OP_SUB }, { "*=", OP_MUL }, { "/=", OP_DIV },
        { "%=", OP_MOD }, { "&=", OP_AND }, { "|=", OP_OR }, { "^=", OP_XOR }, { "<<=", OP_SHL },
        { ">>=", OP_SHR } };
    node_t *n = conditional(p);
    for (size_t i = 0; i < sizeof set / sizeof set[0]; i++)
        if (is(p, set[i].text)) {
            node_t *m = node(p, N_ASSIGN, p->tok.line);
            m->op = (uint8_t)set[i].op;
            next(p);
            deeper(p);
            m->a = n;
            m->b = assignment(p);
            p->depth--;
            return m;
        }
    return n;
}

static node_t *expression(parser_t *p)
{
    node_t *n = assignment(p);
    if (is(p, ",") && p->depth < 0)                 /* the comma operator is not in bm C */
        cc_error(p->cc, p->tok.line, "write one statement for each value");
    return n;
}

/* ---- statements ---- */

static node_t *statement(parser_t *p);

/* { value, value, ... } */
static node_t *value_list(parser_t *p)
{
    node_t *list = node(p, N_LIST, p->tok.line);
    node_t **tail = &list->a;
    expect(p, "{", "before the values");
    while (!accept(p, "}")) {
        *tail = assignment(p);
        tail = &(*tail)->next;
        list->value++;
        if (!is(p, "}"))
            expect(p, ",", "between the values");
    }
    return list;
}

/* The names after a type, to the ';'. Returns the declarations, chained. */
static node_t *declarations(parser_t *p, ctype_t base, int is_const, ctype_t first_type, const char *first,
                            int line)
{
    node_t *head = NULL, **tail = &head;
    for (;;) {
        node_t *d = node(p, S_DECL, line);
        d->type = first ? first_type : stars(p, base);
        d->name = first ? first : name_here(p, "a variable");
        d->op = (uint8_t)is_const;
        first = NULL;
        if (accept(p, "[")) {
            d->type.len = -1;
            if (!accept(p, "]")) {
                d->b = expression(p);               /* worked out with the names, later */
                expect(p, "]", "after the size of the array");
            }
            if (is(p, "["))
                cc_error(p->cc, line, "bm C has arrays of one size: use [row * width + column]");
        }
        if (d->type.base == CT_VOID && !d->type.ptr)
            cc_error(p->cc, line, "a variable cannot be void: use byte or word");
        if (accept(p, "="))
            d->a = is(p, "{") ? value_list(p) : assignment(p);
        *tail = d;
        tail = &d->next;
        if (!accept(p, ","))
            break;
        line = p->tok.line;
    }
    expect(p, ";", "after the declaration");
    return head;
}

static node_t *block(parser_t *p)
{
    node_t *b = node(p, S_BLOCK, p->tok.line);
    node_t **tail = &b->a;
    expect(p, "{", "to open the block");
    while (!accept(p, "}")) {
        if (p->tok.kind == T_END)
            cc_error(p->cc, b->line, "this '{' is never closed: add '}'");
        *tail = statement(p);
        while (*tail)
            tail = &(*tail)->next;
    }
    return b;
}

static node_t *condition(parser_t *p, const char *after)
{
    expect(p, "(", after);
    node_t *n = expression(p);
    if (n->kind == N_ASSIGN && n->op == OP_NONE && !n->wrapped)    /* if ((x = next())) is meant */
        cc_error(p->cc, n->line, "write '==' to compare: '=' gives a value");
    expect(p, ")", "to close the condition");
    return n;
}

static node_t *statement(parser_t *p)
{
    const int line = p->tok.line;
    node_t *n;
    deeper(p);
    if (is(p, "{"))
        n = block(p);
    else if (accept(p, ";"))
        n = node(p, S_EMPTY, line);
    else if (accept(p, "if")) {
        n = node(p, S_IF, line);
        n->a = condition(p, "after if");
        n->b = statement(p);
        if (accept(p, "else"))
            n->c = statement(p);
    } else if (accept(p, "while")) {
        n = node(p, S_WHILE, line);
        n->a = condition(p, "after while");
        n->b = statement(p);
    } else if (accept(p, "do")) {
        n = node(p, S_DO, line);
        n->b = statement(p);
        expect(p, "while", "after the body of do");
        n->a = condition(p, "after while");
        expect(p, ";", "after do ... while (...)");
    } else if (accept(p, "for")) {
        n = node(p, S_FOR, line);
        expect(p, "(", "after for");
        if (starts_type(p)) {
            int is_const;
            const ctype_t base = base_type(p, &is_const);
            n->a = declarations(p, base, is_const, base, NULL, p->tok.line);
        } else if (!accept(p, ";")) {
            n->a = node(p, S_EXPR, p->tok.line);
            n->a->a = expression(p);
            expect(p, ";", "after the start of for");
        }
        if (!is(p, ";"))
            n->b = expression(p);
        expect(p, ";", "after the condition of for");
        if (!is(p, ")"))
            n->c = expression(p);
        expect(p, ")", "to close for (...)");
        n->d = statement(p);
    } else if (accept(p, "break")) {
        n = node(p, S_BREAK, line);
        expect(p, ";", "after break");
    } else if (accept(p, "continue")) {
        n = node(p, S_CONTINUE, line);
        expect(p, ";", "after continue");
    } else if (accept(p, "return")) {
        n = node(p, S_RETURN, line);
        if (!accept(p, ";")) {
            n->a = expression(p);
            expect(p, ";", "after return");
        }
    } else if (accept(p, "asm")) {
        n = node(p, S_ASM, line);
        expect(p, "(", "after asm");
        node_t **tail = &n->a;
        if (p->tok.kind != T_STRING)
            cc_error(p->cc, line, "asm takes the assembly as a string: asm(\"nop\");");
        while (p->tok.kind == T_STRING) {
            *tail = node(p, N_STR, p->tok.line);
            (*tail)->name = p->tok.text;
            (*tail)->value = p->tok.len;
            tail = &(*tail)->next;
            next(p);
            accept(p, ",");                     /* the lines may be a list too */
        }
        expect(p, ")", "to close asm(...)");
        expect(p, ";", "after asm(...)");
    } else if (starts_type(p)) {
        int is_const;
        const ctype_t base = base_type(p, &is_const);
        n = declarations(p, base, is_const, base, NULL, line);
    } else if (is(p, "else"))
        cc_error(p->cc, line, "this else has no if before it");
    else {
        n = node(p, S_EXPR, line);
        n->a = expression(p);
        expect(p, ";", "after the statement");
    }
    p->depth--;
    return n;
}

/* ---- the top of the file ---- */

/* #define NAME value */
static void directive(parser_t *p)
{
    const int line = p->tok.line;
    next(p);                                        /* '#' */
    if (accept(p, "include"))
        cc_error(p->cc, line, "bm C needs no #include: the machine and its names are always there");
    if (!accept(p, "define"))
        cc_error(p->cc, line, "bm C knows #define NAME value only");
    if (p->tok.line != line)
        cc_error(p->cc, line, "#define needs a name and a value on one line");
    const char *name = name_here(p, "a #define");
    if (p->tok.line != line || p->tok.kind == T_END)
        cc_error(p->cc, line, "#define %s needs a value: #define %s 10", name, name);
    if (is(p, "(") && p->tok.text == p->prev.text + p->prev.len)
        cc_error(p->cc, line, "bm C has no #define with arguments: write a function");
    node_t *value = conditional(p);
    if (p->prev.line != line)
        cc_error(p->cc, line, "#define %s: the value stays on the same line", name);
    p->define(p->cc, name, value, line);
}

/* tiles { 0x00, 0xFF, ..., blank(3), ... } */
static void tiles_block(parser_t *p, cc_program_t *prog)
{
    const int line = p->tok.line;
    next(p);
    if (prog->tiles)
        cc_error(p->cc, line, "one tiles { } block is enough: join them");
    node_t *list = node(p, N_LIST, line);
    node_t **tail = &list->a;
    expect(p, "{", "after tiles");
    while (!accept(p, "}")) {
        const int at = p->tok.line;
        if (accept(p, "blank")) {
            *tail = node(p, N_BLANK, at);
            expect(p, "(", "after blank");
            (*tail)->value = cc_constant(p->cc, expression(p));
            expect(p, ")", "after the number of blank tiles");
        } else {
            *tail = node(p, N_NUM, at);
            (*tail)->value = cc_constant(p->cc, assignment(p));
        }
        tail = &(*tail)->next;
        if (!is(p, "}"))
            expect(p, ",", "between the bytes of the tiles");
    }
    prog->tiles = list;
}

void cc_parse(cc_t *cc, cc_program_t *prog, void (*define)(cc_t *, const char *, node_t *, int))
{
    parser_t p = { .cc = cc, .line = 1, .define = define };
    next(&p);
    node_t **tail = &prog->decls;
    while (p.tok.kind != T_END) {
        p.depth = 0;
        if (is(&p, "#")) {
            directive(&p);
            continue;
        }
        if (is(&p, "tiles")) {
            tiles_block(&p, prog);
            continue;
        }
        if (!starts_type(&p))
            cc_error(cc, p.tok.line, "at the top of the file go variables, functions, #define and tiles: found %s",
                     seen(&p));
        int is_const;
        const int line = p.tok.line;
        const ctype_t base = base_type(&p, &is_const);
        const ctype_t type = stars(&p, base);
        const char *name = name_here(&p, "a variable or a function");
        if (!accept(&p, "(")) {
            *tail = declarations(&p, base, is_const, type, name, line);
            while (*tail)
                tail = &(*tail)->next;
            continue;
        }
        node_t *fn = node(&p, S_BLOCK, line);       /* a function: its parameters, then its body */
        fn->name = name;
        fn->type = type;
        fn->op = 1;
        node_t **param = &fn->b;
        int count = 0;
        const parser_t back = p;
        if (accept(&p, "void") && is(&p, ")"))
            ;
        else
            p = back;
        while (!accept(&p, ")")) {
            if (count++)
                expect(&p, ",", "between the parameters");
            if (count > CC_PARAMS)
                cc_error(cc, p.tok.line, "a function takes %d parameters at most", CC_PARAMS);
            int pc;
            const ctype_t pbase = base_type(&p, &pc);
            node_t *d = node(&p, S_DECL, p.tok.line);
            d->type = stars(&p, pbase);
            d->name = name_here(&p, "a parameter");
            if (accept(&p, "[")) {                  /* byte list[]: a pointer */
                expect(&p, "]", "after '[': a parameter takes the address of an array");
                d->type.ptr++;
            }
            if (d->type.base == CT_VOID && !d->type.ptr)
                cc_error(cc, d->line, "a parameter cannot be void");
            *param = d;
            param = &d->next;
        }
        if (accept(&p, ";"))
            continue;                               /* a prototype: bm C finds the functions by itself */
        fn->a = block(&p);
        *tail = fn;
        tail = &fn->next;
    }
}
