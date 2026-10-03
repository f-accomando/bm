/*
 * herobench: what a model of a .bm costs to draw, as Overbit draws its
 * heroes (skeleton, Gouraud, a level of detail), on the ARM rasterizer or
 * with the GPU backend (src/gpu/gpu3d.c on the V3D emulator), at 320x180.
 * For tests/bm/herocost.py, which counts the ARM instructions under
 * qemu-arm (the emulator's own work is not counted: BENCH_EMU_SKIP).
 *
 *   herobench CART.bm MODEL copies distance detail gpu frames
 *
 * copies: heroes in a row across the view, `distance` metres away; detail:
 * 0..3 (R3D_DETAIL); gpu: 0 the ARM, 1 the GPU (built with -DBENCH_GPU).
 * The map is measured by mapbench.c.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/bm.h"
#include "bm/r3d.h"
#ifdef BENCH_GPU
#include "gpu/gpu3d.h"
#include "v3d_emu.h"

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
#endif

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

static int load_model(const bm_cart_t *c, int i, r3d_mesh_t *m, char *name)
{
    bm_model_t md;
    if (bm_mesh_model(c->mesh, c->mesh_size, i, &md) != 0)
        return -1;
    strcpy(name, md.name);
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
    if (c->anim && bm_anim_rig(c->anim, c->anim_size, md.name, &r) == 0) {
        float (*mats)[12] = malloc(r.nbones * sizeof *mats);
        for (int b = 0; b < r.nbones; b++) {
            static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
            memcpy(mats[b], id, sizeof id);
        }
        m->bones = (const float (*)[12])mats;
        m->vbone = r.vbones;
        m->nbones = r.nbones;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 8) {
        fprintf(stderr, "usage: herobench CART.bm MODEL copies distance detail gpu frames\n");
        return 2;
    }
    const int copies = atoi(argv[3]), detail = atoi(argv[5]), gpu = atoi(argv[6]), frames = atoi(argv[7]);
    const float dist = (float)atof(argv[4]);
    size_t len;
    uint8_t *data = read_file(argv[1], &len);
    bm_cart_t c;
    char err[64];
    if (!data || bm_parse(data, len, &c, err, sizeof err) != 0) {
        fprintf(stderr, "herobench: %s\n", data ? err : "cannot read");
        return 1;
    }
    static r3d_mesh_t model;
    char name[32];
    int hero = -1;
    for (int i = 0; hero < 0 && load_model(&c, i, &model, name) == 0; i++) {
        if (!strcmp(name, argv[2]))
            hero = i;
        else
            r3d_mesh_free(&model);
    }
    if (hero < 0) {
        fprintf(stderr, "herobench: no model %s\n", argv[2]);
        return 1;
    }

    static uint16_t fb_ram[320 * 180];
    uint16_t *fb = fb_ram;
    g16_t g;
    r3d_t r;
#ifdef BENCH_GPU
    if (gpu) {
        fb = test_aligned_alloc(64, sizeof fb_ram);
        if (gpu3d_init() != 0) {
            fprintf(stderr, "gpu3d: %s\n", gpu3d_status());
            return 1;
        }
        emu_skip = getenv("BENCH_EMU_SKIP") != NULL;
    }
#else
    if (gpu) {
        fprintf(stderr, "built without BENCH_GPU\n");
        return 1;
    }
#endif
    g16_target(&g, fb, 320, 320, 180, &font);
    r3d_init(&r, &g);
#ifdef BENCH_GPU
    if (gpu)
        r.backend = gpu3d_backend();
#endif
    r3d_light(&r, -0.55f, 0.62f, 0.55f, 0.42f);
    r3d_sky(&r, 0xFFE2BC, 0xA8C0F0, 0x9A8070);
    r3d_shine(&r, 0.9f, 16, 0.25f);
    r3d_fog(&r, 0xC8A890, 40, 160);
    const unsigned flags = R3D_SMOOTH | R3D_DETAIL(detail);
    for (int fr = 0; fr < frames; fr++) {
        g16_cls(&g, g16_rgb(120, 140, 170));
        r3d_zclear(&r);
        r3d_camera(&r, 0, 1.6f, 0, 0.01f * fr, -0.02f, 96);
        for (int i = 0; i < copies; i++) {
            /* a row across the view, all facing the camera */
            const float a = copies > 1 ? (float)i / (copies - 1) - 0.5f : 0;
            const v3_t p = { a * dist * 1.1f, 0, dist };
            r3d_draw_flags(&r, &model, p, 0, 3.14159f, 0, 1, flags);
        }
#ifdef BENCH_GPU
        if (gpu && gpu3d_flush(&g, 0) != 0) {
            fprintf(stderr, "gpu3d: %s (%s)\n", gpu3d_status(), emu_error);
            return 1;
        }
#endif
    }
    printf("herobench %s x%d at %.1f m, detail %d, %s: %u triangles in, %u drawn, %u pixels, %u vertices\n",
           argv[2], copies, dist, detail, gpu ? "GPU" : "ARM", r.tris_in, r.tris_drawn, r.pixels, r.verts);
    return 0;
}
