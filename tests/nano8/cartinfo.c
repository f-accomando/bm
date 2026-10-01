/*
 * nano8 loader on the PC: reads .p8 / .p8.png files and prints what the
 * console would see (version, title, code size, the first code line);
 * --label out.ppm writes the label, --code out.lua the code (P8SCII).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/n8cart.h"

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n + 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

int main(int argc, char **argv)
{
    const char *label = NULL, *code = NULL;
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--label") && i + 1 < argc) { label = argv[++i]; continue; }
        if (!strcmp(argv[i], "--code") && i + 1 < argc) { code = argv[++i]; continue; }
        size_t len;
        uint8_t *d = slurp(argv[i], &len);
        if (!d) {
            printf("%s: cannot read\n", argv[i]);
            bad = 1;
            continue;
        }
        n8_cart_t c;
        char err[80];
        if (n8_cart_load(d, len, 0, &c, err, sizeof err) != 0) {
            printf("%s: %s\n", argv[i], err);
            bad = 1;
            free(d);
            continue;
        }
        char first[61] = "";
        size_t k = 0;
        while (k < c.code_len && k < 60 && c.code[k] != '\n') { first[k] = c.code[k]; k++; }
        first[k] = 0;
        printf("%-28s v%-3d code %6zu  label %s  \"%s\" / \"%s\"  | %s\n", argv[i], c.version, c.code_len,
               c.label ? "yes" : "no ", c.title, c.author, first);
        if (label && c.label) {
            FILE *f = fopen(label, "wb");
            fprintf(f, "P6 128 128 255\n");
            for (int p = 0; p < 128 * 128; p++) {
                uint16_t v = c.label[p];
                fputc((v >> 11) << 3, f); fputc((v >> 5 & 63) << 2, f); fputc((v & 31) << 3, f);
            }
            fclose(f);
        }
        if (code) {
            FILE *f = fopen(code, "wb");
            fwrite(c.code, 1, c.code_len, f);
            fclose(f);
        }
        n8_cart_free(&c);
        free(d);
    }
    return bad;
}
