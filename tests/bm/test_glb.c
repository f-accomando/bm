/*
 * Host test / tool of the .glb reader (src/bm/glb.c): converts a .glb
 * and prints what came out; the model record, its flat twin and the
 * texture go to files for tests/bm/run_glb_test.py to check.
 *
 *   test_glb IN.glb [--faces N] [--height H] [--out PREFIX]
 *     PREFIX.rec (the record), PREFIX.flat (the flat twin, if textured),
 *     PREFIX.rgba (the texture, 256 x 256)
 */
#include "glb.h"

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
        fprintf(stderr, "usage: test_glb IN.glb [--faces N] [--height H] [--out PREFIX]\n");
        return 2;
    }
    glb_opts_t o = { 2.0f, 0, 256 };
    const char *prefix = NULL;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--faces"))
            o.max_faces = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--height"))
            o.height = (float)atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--out"))
            prefix = argv[i + 1];
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
    if (glb_to_model(data, (size_t)n, "thing", &o, &m, err, sizeof err) < 0) {
        printf("error: %s\n", err);
        return 1;
    }
    printf("%d vertices, %d triangles, %s\n", m.nv, m.nf, m.textured ? "textured" : "flat colours");
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
