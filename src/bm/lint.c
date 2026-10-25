/* The build lint of a .b16 (lint.h). The rules are the C table lint_table.h, made by scripts/mklint.py from
 * docs/spec/lint-rules.toml; scripts/mkbm.py reads the same toml. tests/tools/test_mklint.py checks that both agree. */
#include "lint.h"
#include "bm.h"

#include <stdlib.h>
#include <string.h>

enum { BM_LINT_CALL, BM_LINT_PREFIX, BM_LINT_WORD, BM_LINT_DOTTED };

typedef struct {
    int kind;
    const char *name;
    const char *msg;
} bm_lint_rule_t;

#include "lint_table.h"

#define N_RULES ((int)(sizeof bm_lint_b16_rules / sizeof bm_lint_b16_rules[0]))

static unsigned rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* a letter, a digit, "_" (and any byte of a UTF-8 letter) */
static int is_word(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

/* the end of a long bracket that closes with "]" + `eq` times "=" + "]", searched from `from`; n if it does not close */
static size_t long_end(const char *t, size_t n, size_t from, size_t eq)
{
    for (size_t p = from; p + eq + 1 < n; p++) {
        if (t[p] != ']')
            continue;
        size_t e = 0;
        while (e < eq && t[p + 1 + e] == '=')
            e++;
        if (e == eq && t[p + 1 + eq] == ']')
            return p + eq + 2;
    }
    return n;
}

/* the opener "[" "="* "[" at t[i]: its number of "=" and the index after it, or -1 if there is no opener */
static long long_open(const char *t, size_t n, size_t i, size_t *eq)
{
    size_t q = i + 1;
    if (i >= n || t[i] != '[')
        return -1;
    while (q < n && t[q] == '=')
        q++;
    if (q >= n || t[q] != '[')
        return -1;
    *eq = q - i - 1;
    return (long)(q + 1);
}

static void blank_range(char *t, size_t from, size_t to)
{
    for (size_t p = from; p < to; p++)
        if (t[p] != '\n')
            t[p] = ' ';
}

/* the same as lua_code_only in scripts/mkbm.py: in `t` (n bytes) the comments and the insides of the strings
 * become spaces; the line ends stay, so the line numbers stay */
static void bm_lint_blank(char *t, size_t n)
{
    size_t i = 0;
    while (i < n) {
        char c = t[i];
        size_t eq = 0, end;
        long after;
        if (c == '-' && i + 1 < n && t[i + 1] == '-') {
            if ((after = long_open(t, n, i + 2, &eq)) >= 0) {
                end = long_end(t, n, (size_t)after, eq);
            } else {
                end = i;
                while (end < n && t[end] != '\n')
                    end++;
            }
            blank_range(t, i, end);
            i = end;
        } else if (c == '[' && (after = long_open(t, n, i, &eq)) >= 0) {
            end = long_end(t, n, (size_t)after, eq);
            blank_range(t, i, end);
            i = end;
        } else if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && t[j] != c && t[j] != '\n')
                j += t[j] == '\\' ? 2 : 1;
            end = j + 1 < n ? j + 1 : n;
            blank_range(t, i + 1, (end - 1 > i && t[end - 1] == c) ? end - 1 : end);
            i = end;
        } else {
            i++;
        }
    }
}

/* does the rule match at t[p], in a line that starts at t[s] and ends at t[e]? */
static int rule_at(const bm_lint_rule_t *r, const char *t, size_t s, size_t e, size_t p)
{
    size_t k = strlen(r->name);
    unsigned char prev = p > s ? (unsigned char)t[p - 1] : 0;
    if (p + k > e || memcmp(t + p, r->name, k) != 0)
        return 0;
    if (is_word(prev) || (r->kind != BM_LINT_DOTTED && (prev == '.' || prev == ':')))
        return 0;
    p += k;
    switch (r->kind) {
    case BM_LINT_CALL:
        return p < e && t[p] == '(';
    case BM_LINT_PREFIX: {
        size_t q = p;
        while (q < e && is_word((unsigned char)t[q]))
            q++;
        return q > p && q < e && t[q] == '(';
    }
    case BM_LINT_WORD:
        return p >= e || !is_word((unsigned char)t[p]);
    default:
        return 1;
    }
}

static int line_hits(const bm_lint_rule_t *r, const char *t, size_t s, size_t e)
{
    for (size_t p = s; p < e; p++)
        if (rule_at(r, t, s, e, p))
            return 1;
    return 0;
}

/* the header facts that do not suit the RGB30 (b16_profile_warnings in scripts/mkbm.py): any screen size and
 * any list of kernels is valid, only a need for a mouse is a warning */
static int profile(const uint8_t *c, bm_lint_fn warn, void *ctx)
{
    int n = 0;
    if (rd16(c + 18) & BM_FLAG_COMPAT) {
        unsigned needs = c[BM_COMPAT_AT + 3];
        if (needs & 8) {
            warn(ctx, 0, "needs mouse_hw: the RGB30 has no mouse");
            n++;
        }
    }
    return n;
}

int bm_cart_lint(const uint8_t *cart, size_t len, bm_lint_fn warn, void *ctx)
{
    if (len < BM_HEADER_SIZE || (memcmp(cart, "BMCART\0\0", 8) != 0 && memcmp(cart, "BM33CART", 8) != 0))
        return -1;
    int total = profile(cart, warn, ctx);
    unsigned nsec = cart[17];
    size_t text_n = 0;
    for (unsigned i = 0; i < nsec; i++) {
        size_t at = BM_HEADER_SIZE + (size_t)i * 16;
        if (at + 16 > len)
            break;
        size_t off = rd32(cart + at + 4), size = rd32(cart + at + 8);
        if (rd32(cart + at) == BM_SEC_LUA && off <= len && size <= len - off)
            text_n += size + 1;
    }
    if (!text_n)
        return total;
    char *t = malloc(text_n + 1);
    if (!t)
        return total;
    size_t used = 0;
    for (unsigned i = 0; i < nsec; i++) {
        size_t at = BM_HEADER_SIZE + (size_t)i * 16;
        if (at + 16 > len)
            break;
        size_t off = rd32(cart + at + 4), size = rd32(cart + at + 8);
        if (rd32(cart + at) != BM_SEC_LUA || off > len || size > len - off)
            continue;
        if (used)
            t[used++] = '\n';           /* the sections are joined by a line end, as in mkbm.py */
        memcpy(t + used, cart + off, size);
        used += size;
    }
    bm_lint_blank(t, used);
    for (int r = 0; r < N_RULES; r++) {
        int line = 1;
        for (size_t s = 0; s <= used; line++) {
            size_t e = s;
            while (e < used && t[e] != '\n')
                e++;
            if (line_hits(&bm_lint_b16_rules[r], t, s, e)) {
                warn(ctx, line, bm_lint_b16_rules[r].msg);
                total++;
                break;
            }
            s = e + 1;
        }
    }
    free(t);
    return total;
}
