/* The tokens of a cartridge's Lua code (tokens.h): a small Lua 5.4 lexer
 * that only counts. Portable C, tried on the PC by tests/bm/test_tokens.c. */

#include "tokens.h"

#include <string.h>

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }
static int is_xdigit(int c) { return is_digit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }

/* [==[ at s[i]: the number of '=' (level), or -1 if it is not a long bracket */
static int long_open(const char *s, size_t n, size_t i)
{
    if (i >= n || s[i] != '[')
        return -1;
    size_t j = i + 1;
    while (j < n && s[j] == '=')
        j++;
    return j < n && s[j] == '[' ? (int)(j - i - 1) : -1;
}

/* past the ]==] that closes a long bracket of `level` opened at s[i] */
static size_t long_skip(const char *s, size_t n, size_t i, int level)
{
    i += (size_t)level + 2;
    while (i < n) {
        if (s[i] == ']') {
            size_t j = i + 1;
            while (j < n && s[j] == '=')
                j++;
            if (j < n && s[j] == ']' && (int)(j - i - 1) == level)
                return j + 1;
        }
        i++;
    }
    return n;
}

static int word_is(const char *s, size_t len, const char *w)
{
    return strlen(w) == len && memcmp(s, w, len) == 0;
}

static int is_keyword(const char *s, size_t len)
{
    static const char *const kw[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
    };
    for (size_t k = 0; k < sizeof kw / sizeof kw[0]; k++)
        if (word_is(s, len, kw[k]))
            return 1;
    return 0;
}

int lua_tokens(const char *s, size_t n)
{
    int count = 0;
    int operand = 0;                    /* the last token ends an operand: a - after it is binary */
    size_t i = 0;
    while (i < n) {
        const int c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            i++;
            continue;
        }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') {            /* a comment */
            int level = long_open(s, n, i + 2);
            if (level >= 0) {
                i = long_skip(s, n, i + 2, level);
            } else {
                while (i < n && s[i] != '\n')
                    i++;
            }
            continue;
        }
        if (is_alpha(c)) {
            size_t j = i;
            while (j < n && is_alnum((unsigned char)s[j]))
                j++;
            const size_t len = j - i;
            const int kw = is_keyword(s + i, len);
            if (!(kw && (word_is(s + i, len, "end") || word_is(s + i, len, "local"))))
                count++;
            operand = !kw || word_is(s + i, len, "true") || word_is(s + i, len, "false") ||
                      word_is(s + i, len, "nil") || word_is(s + i, len, "end");
            i = j;
            continue;
        }
        if (is_digit(c) || (c == '.' && i + 1 < n && is_digit((unsigned char)s[i + 1]))) {
            size_t j = i;
            if (c == '0' && j + 1 < n && (s[j + 1] | 32) == 'x') {
                j += 2;
                while (j < n && (is_xdigit((unsigned char)s[j]) || s[j] == '.' ||
                                 ((s[j] | 32) == 'p') ||
                                 ((s[j] == '+' || s[j] == '-') && (s[j - 1] | 32) == 'p')))
                    j++;
            } else {
                while (j < n && (is_digit((unsigned char)s[j]) || s[j] == '.' || (s[j] | 32) == 'e' ||
                                 ((s[j] == '+' || s[j] == '-') && (s[j - 1] | 32) == 'e')))
                    j++;
            }
            count++;
            operand = 1;
            i = j;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && s[j] != c && s[j] != '\n') {
                if (s[j] == '\\' && j + 1 < n)
                    j++;                /* \" \\ and \<newline> */
                j++;
            }
            count++;
            operand = 1;
            i = j < n ? j + 1 : n;
            continue;
        }
        if (c == '[') {
            int level = long_open(s, n, i);
            if (level >= 0) {               /* a long string */
                count++;
                operand = 1;
                i = long_skip(s, n, i, level);
                continue;
            }
        }
        /* punctuation and operators */
        if (c == ',' || c == ';' || c == ')' || c == ']' || c == '}') {
            operand = c == ')' || c == ']' || c == '}';
            i++;
            continue;
        }
        if (c == ':') {
            i += i + 1 < n && s[i + 1] == ':' ? 2 : 1;            /* : and :: are free */
            operand = 0;
            continue;
        }
        if (c == '.') {
            if (i + 2 < n && s[i + 1] == '.' && s[i + 2] == '.') {  /* ... */
                count++;
                operand = 1;
                i += 3;
            } else if (i + 1 < n && s[i + 1] == '.') {             /* .. */
                count++;
                operand = 0;
                i += 2;
            } else {                                                /* a.b: free */
                operand = 0;
                i++;
            }
            continue;
        }
        if (c == '-' && !operand) {
            /* -1: the sign and the number are one token (the number's) */
            size_t j = i + 1;
            while (j < n && (s[j] == ' ' || s[j] == '\t'))
                j++;
            if (j < n && (is_digit((unsigned char)s[j]) ||
                          (s[j] == '.' && j + 1 < n && is_digit((unsigned char)s[j + 1])))) {
                i = j;
                continue;
            }
        }
        /* two-character operators are one token */
        if (i + 1 < n) {
            const char d = s[i + 1];
            if ((d == '=' && (c == '=' || c == '~' || c == '<' || c == '>')) ||
                (c == '/' && d == '/') || (c == '<' && d == '<') || (c == '>' && d == '>')) {
                count++;
                operand = 0;
                i += 2;
                continue;
            }
        }
        count++;                            /* + - * / % ^ # & ~ | < > = ( { [ and the rest */
        operand = 0;
        i++;
    }
    return count;
}
