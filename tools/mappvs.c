/*
 * mappvs: which chunks of a map can be seen from where (the potentially
 * visible sets of Overbit's maps, carts/overbit/art/mapbake.py). The chunks
 * are drawn by the console's own rasterizer (r3d), each in a colour that is
 * its number, from every sample point, into the six faces of a cube; the
 * numbers left on the screen are what that point sees.
 *
 *   mappvs CART.bm IN.txt OUT.txt
 *
 * IN.txt:  "chunks N", N model names, "samples M", M lines "cell x y z"
 * OUT.txt: one line per cell that has samples: "cell i j k ..." (0-based
 *          chunk numbers)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/bm.h"
#include "bm/r3d.h"

#define SIDE 128
#define MAXC 512

static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    *len = fread(d, 1, (size_t)n, f);
    fclose(f);
    return d;
}

/* a chunk with every face in the colour of its number (flat, no light) */
static int load(const bm_cart_t *c, const char *name, int id, r3d_mesh_t *m)
{
    bm_model_t md;
    for (int i = 0; bm_mesh_model(c->mesh, c->mesh_size, i, &md) == 0; i++) {
        if (strcmp(md.name, name))
            continue;
        if (r3d_mesh_alloc(m, md.nverts, md.nfaces) != 0)
            return -1;
        for (int v = 0; v < md.nverts; v++) {
            float xyz[3];
            bm_model_vertex(&md, v, xyz);
            m->verts[v] = (v3_t){ xyz[0], xyz[1], xyz[2] };
        }
        uint32_t code = (uint32_t)id + 1;
        uint32_t rgb = ((code & 31) << 3) << 16 | (((code >> 5) & 63) << 2) << 8;
        for (int f = 0; f < md.nfaces; f++) {
            uint32_t col;
            float uv[6];
            bm_model_face(&md, f, m->faces + f * 3, &col, uv);
            m->colors[f] = rgb | (col & R3D_SCREEN);
        }
        r3d_mesh_normals(m);
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: mappvs CART.bm IN.txt OUT.txt\n");
        return 2;
    }
    size_t len;
    uint8_t *data = read_file(argv[1], &len);
    bm_cart_t c;
    char err[64];
    if (!data || bm_parse(data, len, &c, err, sizeof err) != 0) {
        fprintf(stderr, "mappvs: %s\n", data ? err : "cannot read");
        return 1;
    }
    FILE *in = fopen(argv[2], "r");
    if (!in) {
        fprintf(stderr, "mappvs: cannot read %s\n", argv[2]);
        return 1;
    }
    int nc = 0;
    if (fscanf(in, " chunks %d", &nc) != 1 || nc <= 0 || nc > MAXC) {
        fprintf(stderr, "mappvs: bad chunks\n");
        return 1;
    }
    static r3d_mesh_t mesh[MAXC];
    for (int i = 0; i < nc; i++) {
        char name[64];
        if (fscanf(in, " %63s", name) != 1 || load(&c, name, i, &mesh[i]) != 0) {
            fprintf(stderr, "mappvs: no model %s\n", name);
            return 1;
        }
    }
    int ns = 0;
    if (fscanf(in, " samples %d", &ns) != 1 || ns < 0) {
        fprintf(stderr, "mappvs: bad samples\n");
        return 1;
    }
    static uint16_t fb[SIDE * SIDE];
    g16_t g;
    r3d_t r;
    g16_target(&g, fb, SIDE, SIDE, SIDE, &font);
    r3d_init(&r, &g);
    FILE *out = fopen(argv[3], "w");
    int cur = -1;
    static uint8_t seen[MAXC];
    static const float dirs[6][2] = { { 0, 0 }, { 1.5708f, 0 }, { 3.14159f, 0 }, { -1.5708f, 0 },
                                      { 0, 1.5f }, { 0, -1.5f } };
    for (int s = 0; s < ns; s++) {
        int cell;
        float x, y, z;
        if (fscanf(in, " %d %f %f %f", &cell, &x, &y, &z) != 4)
            break;
        if (cell != cur) {
            if (cur >= 0) {
                fprintf(out, "%d", cur);
                for (int i = 0; i < nc; i++)
                    if (seen[i]) fprintf(out, " %d", i);
                fprintf(out, "\n");
            }
            memset(seen, 0, sizeof seen);
            cur = cell;
        }
        for (int d = 0; d < 6; d++) {
            memset(fb, 0, sizeof fb);
            r3d_zclear(&r);
            r3d_camera(&r, x, y, z, dirs[d][0], dirs[d][1], 92);
            for (int i = 0; i < nc; i++)
                r3d_draw_flags(&r, &mesh[i], (v3_t){ 0, 0, 0 }, 0, 0, 0, 1, R3D_UNLIT);
            for (int p = 0; p < SIDE * SIDE; p++) {
                uint16_t px = fb[p];
                if (!px) continue;
                int code = (px >> 11) | ((px >> 5) & 63) << 5;
                if (code >= 1 && code <= nc)
                    seen[code - 1] = 1;
            }
        }
    }
    if (cur >= 0) {
        fprintf(out, "%d", cur);
        for (int i = 0; i < nc; i++)
            if (seen[i]) fprintf(out, " %d", i);
        fprintf(out, "\n");
    }
    fclose(out);
    return 0;
}
