/* Host program for the build lint of a .b16 (bm_b16_lint, src/bm/lint.c).
 *
 *   test_lint FILE     prints one line "LINE|message" for each warning (LINE is 0 for a fact of the header);
 *                      tests/tools/test_mklint.py compares it with scripts/mkbm.py b16_lint */
#include <stdio.h>
#include <stdlib.h>

#include "bm/lint.h"

static void say(void *ctx, int line, const char *msg)
{
    (void)ctx;
    printf("%d|%s\n", line, msg);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_lint FILE\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "read error\n");
        return 2;
    }
    fclose(f);
    int r = bm_b16_lint(buf, (size_t)n, say, NULL);
    free(buf);
    return r < 0 ? 1 : 0;
}
