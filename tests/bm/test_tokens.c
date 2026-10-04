/* The token counter of the dev kit (src/bm/tokens.c) on the PC: the rules
 * of tokens.h on small pieces of code, then a whole cartridge if given.
 *
 *   test_tokens [main.lua ...]          (make test-bm)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokens.h"

static int fails;

static void expect(const char *src, int want)
{
    int got = lua_tokens(src, strlen(src));
    if (got != want) {
        printf("FAIL tokens of \"%s\": %d, not %d\n", src, got, want);
        fails++;
    }
}

int main(int argc, char **argv)
{
    expect("", 0);
    expect("x = 1", 3);
    expect("local x = -1", 3);                          /* local free, -1 one token */
    expect("print(\"hi\")", 3);                         /* ) free */
    expect("function f(a, b) return a + b end", 9);     /* , ) end free */
    expect("a.b.c = {1, 2}", 7);                        /* . , } free */
    expect("x = y - 1", 5);                             /* a binary minus counts */
    expect("x = y-1", 5);
    expect("x = - y", 4);                               /* a minus before a name counts */
    expect("x = (-2)", 4);
    expect("t[i] = s .. \"a\"", 7);                     /* ] free, .. one token */
    expect("if a ~= b then end", 5);
    expect("x = 0x1F + 1e3 + 2.5e-3", 7);
    expect("x = 0xA.8p-1", 3);
    expect("obj:m(...)", 4);                            /* : free, ... one token */
    expect("::top:: goto top", 3);                      /* :: free */
    expect("-- a comment\nx = 1 -- another", 3);
    expect("--[[ a long\ncomment ]] x = [[a long\nstring]]", 3);
    expect("s = [==[ a ]] b ]==] t = 1", 6);           /* the level of the bracket */
    expect("s = \"a \\\" b\" .. 'c\\'d'", 5);           /* escaped quotes */
    expect("x = a // b << 2 >= c", 9);
    expect("for i = 1, 10 do sum = sum + i end", 11);
    expect("t = {x = 1; y = 2}", 9);                    /* ; free */
    expect("return #t, not ok, true, nil", 7);

    for (int k = 1; k < argc; k++) {
        FILE *f = fopen(argv[k], "rb");
        if (!f) {
            printf("FAIL cannot open %s\n", argv[k]);
            fails++;
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *s = malloc((size_t)n + 1);
        if (!s || fread(s, 1, (size_t)n, f) != (size_t)n) {
            printf("FAIL cannot read %s\n", argv[k]);
            fails++;
        } else {
            int t = lua_tokens(s, (size_t)n);
            printf("%s: %d tokens, %ld bytes\n", argv[k], t, n);
            if (t <= 0 || t > n) {
                printf("FAIL %s: %d tokens for %ld bytes\n", argv[k], t, n);
                fails++;
            }
        }
        free(s);
        fclose(f);
    }
    printf("test_tokens: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
