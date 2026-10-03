/*
 * r3dbench: the 3D rasterizer on a scene like Overbit's (a floor, some boxes,
 * N copies of a model of a .bm at rest), for profiling the ARM code with
 * tools/armprof.py (built for ARM Linux, run by qemu-arm) or on the PC.
 *
 *   r3dbench CART.bm MODEL [copies] [distance] [flags] [frames] [out.ppm]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bm/bm.h"
#include "bm/r3d.h"

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

static int load_model(const bm_cart_t *c, const char *name, r3d_mesh_t *m, float (**mats)[12])
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
        for (int f = 0; f < md.nfaces; f++) {
            uint32_t col;
            float uv[6];
            bm_model_face(&md, f, m->faces + f * 3, &col, uv);
            m->colors[f] = col;
        }
        r3d_mesh_normals(m);
        bm_rig_t r;
        if (c->anim && bm_anim_rig(c->anim, c->anim_size, name, &r) == 0) {
            *mats = malloc(r.nbones * sizeof **mats);
            for (int b = 0; b < r.nbones; b++) {
                static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                memcpy((*mats)[b], id, sizeof id);
            }
            m->bones = (const float (*)[12])*mats;
            m->vbone = r.vbones;
            m->nbones = r.nbones;
        }
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: r3dbench CART.bm MODEL [copies] [distance] [flags] [frames] [out.ppm]\n");
        return 2;
    }
    int copies = argc > 3 ? atoi(argv[3]) : 4;
    float dist = argc > 4 ? (float)atof(argv[4]) : 10;
    unsigned flags = argc > 5 ? (unsigned)atoi(argv[5]) : 4;
    int frames = argc > 6 ? atoi(argv[6]) : 10;
    size_t len;
    uint8_t *data = read_file(argv[1], &len);
    bm_cart_t c;
    char err[64];
    if (!data || bm_parse(data, len, &c, err, sizeof err) != 0) {
        fprintf(stderr, "r3dbench: %s\n", data ? err : "cannot read");
        return 1;
    }
    r3d_mesh_t m, floor_m, box;
    float (*mats)[12] = NULL;
    if (load_model(&c, argv[2], &m, &mats) != 0) {
        fprintf(stderr, "r3dbench: no model %s\n", argv[2]);
        return 1;
    }
    r3d_mesh_alloc(&floor_m, 4, 2);
    floor_m.verts[0] = (v3_t){ -40, 0, -40 }; floor_m.verts[1] = (v3_t){ -40, 0, 40 };
    floor_m.verts[2] = (v3_t){ 40, 0, 40 }; floor_m.verts[3] = (v3_t){ 40, 0, -40 };
    uint16_t fi[6] = { 0, 1, 2, 0, 2, 3 };
    memcpy(floor_m.faces, fi, sizeof fi);
    floor_m.colors[0] = floor_m.colors[1] = 0x6E737A;
    r3d_mesh_normals(&floor_m);
    r3d_mesh_cube(&box, 0x7A8794);

    static uint16_t fb[320 * 180];
    g16_t g;
    r3d_t r;
    g16_target(&g, fb, 320, 320, 180, &font);
    r3d_init(&r, &g);
    r3d_light(&r, -0.55f, 0.62f, 0.55f, 0.42f);
    r3d_sky(&r, 0xFFE2BC, 0xA8C0F0, 0x9A8070);
    r3d_shine(&r, 0.9f, 16, 0.25f);
    uint32_t tris = 0, px = 0;
    for (int fr = 0; fr < frames; fr++) {
        g16_cls(&g, g16_rgb(120, 140, 170));
        r3d_zclear(&r);
        r3d_camera(&r, 0, 2.25f, -dist * 0.4f, 0.02f * fr, -0.05f, 96);
        r3d_draw_flags(&r, &floor_m, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1, R3D_NOZ);
        for (int i = 0; i < 6; i++)
            r3d_draw(&r, &box, (v3_t){ -8.0f + i * 3.2f, 1, 6 + (i % 3) * 4.0f }, 0, 0, 0, 1);
        for (int i = 0; i < copies; i++) {
            float a = (float)i / (copies > 1 ? copies - 1 : 1) - 0.5f;
            v3_t p = { a * dist * 1.2f, 0, dist * (0.6f + 0.4f * (i % 2)) };
            if (flags & R3D_SHADOW)
                r3d_draw_flags(&r, &m, p, 0, 3.14159f + a, 0, 1, (flags & ~R3D_SMOOTH) | R3D_SHADOW);
            r3d_draw_flags(&r, &m, p, 0, 3.14159f + a, 0, 1, flags & ~R3D_SHADOW);
        }
        tris = r.tris_drawn;
        px = r.pixels;
    }
    printf("r3dbench: %s x%d at %.0f m, flags %u: %u triangles, %u pixels per frame, %d frames\n",
           argv[2], copies, dist, flags, tris, px, frames);
    if (argc > 7) {
        FILE *f = fopen(argv[7], "wb");
        fprintf(f, "P6 320 180 255\n");
        for (int i = 0; i < 320 * 180; i++) {
            uint32_t cc = g16_to_rgb24(fb[i]);
            fputc(cc >> 16, f); fputc(cc >> 8 & 255, f); fputc(cc & 255, f);
        }
        fclose(f);
    }
    return 0;
}
