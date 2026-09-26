#include "selftest.h"
#include "lib/printf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        fails++;
        kprintf("  \x1b[91mFAIL\x1b[0m %s\n", what);
    }
}

static void test_stdio(void)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%d %s %.3f %e", -42, "ok", 3.14159265, 1.5e-7);
    check(strcmp(buf, "-42 ok 3.142 1.500000e-07") == 0, "snprintf");
    check(strtod("2.5e3", NULL) == 2500.0, "strtod");
    check(atoi("  123") == 123, "atoi");
}

static void test_math(void)
{
    check(fabs(sqrt(2.0) - 1.41421356237) < 1e-9, "sqrt");
    check(fabs(sin(M_PI / 2) - 1.0) < 1e-12, "sin");
    check(fabs(pow(2.0, 10.0) - 1024.0) < 1e-9, "pow");
    check(floor(-1.5) == -2.0 && fmod(7.0, 3.0) == 1.0, "floor/fmod");
}

/* Allocates blocks of pseudo-random sizes, fills them with a pattern,
 * frees half, reallocates, and verifies nothing got overwritten. */
static void test_malloc(void)
{
    enum { N = 256 };
    static uint8_t *blk[N];
    static uint32_t len[N];
    uint32_t seed = 12345;

    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        len[i] = 1 + (seed >> 16) % 4000;
        blk[i] = malloc(len[i]);
        check(blk[i] != NULL, "malloc");
        if (!blk[i])
            return;
        memset(blk[i], (uint8_t)i, len[i]);
    }
    for (int i = 0; i < N; i += 2) {
        free(blk[i]);
        blk[i] = NULL;
    }
    for (int i = 0; i < N; i += 2) {
        blk[i] = calloc(1, len[i]);
        check(blk[i] != NULL, "calloc");
        if (blk[i])
            memset(blk[i], (uint8_t)i, len[i]);
    }
    for (int i = 0; i < N; i++) {
        int ok = blk[i] != NULL;
        for (uint32_t j = 0; ok && j < len[i]; j++)
            ok = blk[i][j] == (uint8_t)i;
        check(ok, "heap block corrupted");
        free(blk[i]);
    }

    void *big = malloc(64u << 20);          /* 64 MiB */
    check(big != NULL, "64 MiB malloc");
    free(big);
}

int libc_selftest(void)
{
    fails = 0;
    test_stdio();
    test_math();
    test_malloc();

    printf("libc selftest: %s\x1b[0m (stdio, libm, malloc); printf %.3f, sqrt(2) %.9f\n",
           fails ? "\x1b[91mFAILED" : "\x1b[92mok", 3.14159265, sqrt(2.0));
    fflush(stdout);
    return fails;
}
