/*
 * Host test / tool of bm_res_build (src/bm/format.c): the resource file the console's res_write makes.
 * tests/bm/run_resbuild_test.py builds the same file with scripts/bmres.py and compares the bytes.
 *
 *   test_resbuild OUT KIND NAME AUTHOR TYPE:FILE...
 *
 * The sections go in the order given. The result is read again with bm_parse_any before it is written.
 */
#include "bm/bm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(len ? (size_t)len : 1);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len)
        return NULL;
    fclose(f);
    *n = (size_t)len;
    return d;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: test_resbuild OUT KIND NAME AUTHOR TYPE:FILE...\n");
        return 2;
    }
    bm_put_t put[32];
    int n = 0;
    for (int i = 5; i < argc && n < 32; i++, n++) {
        char *colon = strchr(argv[i], ':');
        size_t len = 0;
        if (!colon || !(put[n].data = slurp(colon + 1, &len))) {
            fprintf(stderr, "bad section %s\n", argv[i]);
            return 2;
        }
        put[n].type = (uint32_t)atoi(argv[i]);
        put[n].size = (uint32_t)len;
    }
    size_t out_len = 0;
    uint8_t *out = bm_res_build((unsigned)atoi(argv[2]), argv[3], argv[4], put, n, &out_len);
    if (!out) {
        fprintf(stderr, "bm_res_build: NULL\n");
        return 1;
    }
    bm_cart_t c;
    char err[96];
    if (bm_parse_any(out, out_len, &c, err, sizeof err) != 0) {
        fprintf(stderr, "bm_parse_any: %s\n", err);
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(out, 1, out_len, f) != out_len)
        return 1;
    fclose(f);
    printf("kind %d, %zu bytes\n", c.kind, out_len);
    return 0;
}
