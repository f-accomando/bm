/*
 * Host tool / test of the outline maker (src/bm/cutout.c): a picture
 * becomes a cutout or a lathe; the record, its flat twin and the sheet
 * texture go to files for tests/bm/run_cutout_test.py to check.
 *
 *   test_cutout IN.png|IN.jpg [--lathe] [--depth D] [--segments N] [--faces N] [--height H] [--out PREFIX]
 */
#include "cutout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(const char *prefix, const char *ext, const uint8_t *data, size_t len)
{
    char path[512];
    snprintf(path, sizeof path, "%s.%s", prefix, ext);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_cutout IN.png [--lathe] [--depth D] [--segments N] [--faces N] [--height H] [--out PREFIX]\n");
        return 2;
    }
    cutout_opts_t o = { 0, 2.0f, 0.2f, 12, 1200, 256, 0.02f };
    const char *prefix = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--lathe"))
            o.lathe = 1;
        else if (i + 1 < argc && !strcmp(argv[i], "--depth"))
            o.depth = (float)atof(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--segments"))
            o.segments = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--faces"))
            o.max_faces = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--height"))
            o.height = (float)atof(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--tolerance"))
            o.tolerance = (float)atof(argv[++i]);
        else if (i + 1 < argc && !strcmp(argv[i], "--out"))
            prefix = argv[++i];
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (fread(data, 1, (size_t)n, f) != (size_t)n)
        n = 0;
    fclose(f);
    glb_model_t m;
    char err[96];
    if (cutout_from_file(data, (size_t)n, "thing", &o, &m, err, sizeof err) < 0) {
        printf("error: %s\n", err);
        return 1;
    }
    printf("%d vertices, %d triangles, %s\n", m.nv, m.nf, o.lathe ? "lathe" : "cutout");
    if (prefix) {
        save(prefix, "rec", m.record, m.record_len);
        if (m.flat)
            save(prefix, "flat", m.flat, m.flat_len);
        if (m.texture)
            save(prefix, "rgba", m.texture, 256 * 256 * 4);
    }
    glb_model_free(&m);
    free(data);
    return 0;
}
