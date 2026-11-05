/* The C reader of .bmz (src/bm/bmz.c) on files made by scripts/bmz.py.
 *   test_bmz GOOD.bmz RAW        the file must extract to RAW, byte for byte
 *   test_bmz BAD.bmz CODE        the file must fail with the named error (parse or extract)
 */
#include "bm/bmz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n ? (size_t)n : 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n)
        return NULL;
    fclose(f);
    *len = (size_t)n;
    return b;
}

static int code_of(const char *name)
{
    static const struct { const char *n; int c; } t[] = {
        { "SHORT", BMZ_E_SHORT }, { "MAGIC", BMZ_E_MAGIC }, { "VERSION", BMZ_E_VERSION }, { "SIZE", BMZ_E_SIZE },
        { "NAME", BMZ_E_NAME }, { "INFLATE", BMZ_E_INFLATE }, { "CRC", BMZ_E_CRC }, { "KIND", BMZ_E_KIND },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(name, t[i].n))
            return t[i].c;
    return 1;
}

int main(int argc, char **argv)
{
    size_t flen, rlen = 0;
    uint8_t *file, *want = NULL, *out;
    bmz_info_t info;
    int expect = 0, r;

    if (argc != 3)
        return 2;
    file = slurp(argv[1], &flen);
    if (!file)
        return 2;
    if (argv[2][0] >= 'A' && argv[2][0] <= 'Z')    /* an error name, not a file */
        expect = code_of(argv[2]);
    else
        want = slurp(argv[2], &rlen);
    r = bmz_parse(file, flen, &info);
    if (r == BMZ_OK) {
        out = malloc(info.raw_len);
        r = bmz_extract(&info, out);
        if (r == BMZ_OK && want && (info.raw_len != rlen || memcmp(out, want, rlen)))
            r = 1;
        free(out);
    }
    if (want) {
        if (r != BMZ_OK) {
            printf("FAIL %s: %s\n", argv[1], bmz_error(r));
            return 1;
        }
        printf("ok   %s (%u bytes, %s -> %s)\n", argv[1], (unsigned)info.raw_len, info.name, info.dir);
        return 0;
    }
    if (r != expect) {
        printf("FAIL %s: expected %d, got %d (%s)\n", argv[1], expect, r, bmz_error(r));
        return 1;
    }
    printf("ok   %s refused: %s\n", argv[1], bmz_error(r));
    return 0;
}
