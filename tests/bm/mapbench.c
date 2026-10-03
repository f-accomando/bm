/*
 * mapbench: the 3D rasterizer drawing a baked map (the "lit" models of a
 * .bm whose names start with a prefix) from a point of view, every chunk,
 * for profiling the ARM code with tools/armprof.py (built for ARM Linux,
 * run by qemu-arm) or on the PC.
 *
 *   mapbench CART.bm PREFIX|@LIST x y z yaw pitch [frames] [out.ppm]
 *
 * Built with -DBENCH_GPU (and tests/gpu/v3d_emu.c, src/gpu/gpu3d.c, as
 * tests/bm/herocost.py does), MAPBENCH_GPU=1 draws with the GPU backend on
 * the V3D emulator: the ARM's share of the map with the GPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bm/bm.h"
#include "bm/r3d.h"
#ifdef BENCH_GPU
#include <stdarg.h>
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

static g16_sheet_t sheet;

static void sheet_set(void *ctx, int x, int y, const uint8_t rgba[4])
{
    (void)ctx;
    g16_sheet_set(&sheet, x, y, g16_rgb(rgba[0], rgba[1], rgba[2]), rgba[3] >= 128);
}

static void load(const bm_model_t *md, r3d_mesh_t *m)
{
    r3d_mesh_alloc(m, md->nverts, md->nfaces);
    for (int v = 0; v < md->nverts; v++) {
        float xyz[3];
        bm_model_vertex(md, v, xyz);
        m->verts[v] = (v3_t){ xyz[0], xyz[1], xyz[2] };
    }
    for (int f = 0; f < md->nfaces; f++) {
        uint32_t col;
        float uv[6];
        bm_model_face(md, f, m->faces + f * 3, &col, uv);
        if (col & R3D_TEXTURED) {
            col &= 0xFF000000u;
            if (!m->uv) {
                r3d_mesh_alloc_uv(m);
                m->tex = &sheet;
            }
            memcpy(m->uv + f * 6, uv, sizeof uv);
        }
        m->colors[f] = col;
    }
    if (md->flags & BM_MODEL_LIT) {
        m->clight = malloc((size_t)md->nfaces * 9);
        for (int f = 0; f < md->nfaces; f++)
            bm_model_face_light(md, f, m->clight + f * 9);
    }
    r3d_mesh_normals(m);
}

int main(int argc, char **argv)
{
    if (argc < 8) {
        fprintf(stderr, "usage: mapbench CART.bm PREFIX x y z yaw pitch [frames] [out.ppm]\n");
        return 2;
    }
    size_t len;
    uint8_t *data = read_file(argv[1], &len);
    bm_cart_t c;
    char err[64];
    if (!data || bm_parse(data, len, &c, err, sizeof err) != 0) {
        fprintf(stderr, "mapbench: %s\n", data ? err : "cannot read");
        return 1;
    }
    if (c.sheet8) {
        g16_sheet_alloc(&sheet, c.sheet_w, c.sheet_h);
        bm_sheet8_unpack(&c, sheet_set, NULL);
    } else {
        g16_sheet_alloc(&sheet, 8, 8);
    }
    static r3d_mesh_t mesh[256];
    int n = 0;
    bm_model_t md;
    size_t pl = strlen(argv[2]);
    if (argv[2][0] == '@') {
        /* a file with the names of the models to draw, one a line */
        FILE *lf = fopen(argv[2] + 1, "r");
        char name[64];
        while (lf && fscanf(lf, " %63s", name) == 1 && n < 256)
            for (int i = 0; bm_mesh_model(c.mesh, c.mesh_size, i, &md) == 0; i++)
                if (!strcmp(md.name, name)) {
                    load(&md, &mesh[n++]);
                    break;
                }
        if (lf) fclose(lf);
    } else {
        for (int i = 0; bm_mesh_model(c.mesh, c.mesh_size, i, &md) == 0 && n < 256; i++)
            if (!strncmp(md.name, argv[2], pl))
                load(&md, &mesh[n++]);
    }
    float x = (float)atof(argv[3]), y = (float)atof(argv[4]), z = (float)atof(argv[5]);
    float yaw = (float)atof(argv[6]), pitch = (float)atof(argv[7]);
    int frames = argc > 8 ? atoi(argv[8]) : 4;

    static uint16_t fb_ram[320 * 180];
    uint16_t *fb = fb_ram;
    g16_t g;
    r3d_t r;
#ifdef BENCH_GPU
    const int gpu = getenv("MAPBENCH_GPU") != NULL;
    if (gpu) {
        fb = test_aligned_alloc(64, sizeof fb_ram);
        if (gpu3d_init() != 0) {
            fprintf(stderr, "gpu3d: %s\n", gpu3d_status());
            return 1;
        }
        emu_skip = getenv("BENCH_EMU_SKIP") != NULL;
    }
#endif
    g16_target(&g, fb, 320, 320, 180, &font);
    r3d_init(&r, &g);
#ifdef BENCH_GPU
    if (gpu)
        r.backend = gpu3d_backend();
#endif
    r3d_light(&r, -0.78f, 0.36f, -0.5f, 0.42f);
    r3d_fog(&r, 0xD8A0A0, 45, 150);
    uint32_t tris = 0, drawn = 0, px = 0, verts = 0;
    for (int fr = 0; fr < frames; fr++) {
        g16_cls(&g, g16_rgb(200, 150, 140));
        r3d_zclear(&r);
        r.tris_in = r.tris_drawn = r.pixels = r.verts = 0;
        r3d_camera(&r, x, y, z, yaw, pitch, 96);
        for (int i = 0; i < n; i++)
            r3d_draw_flags(&r, &mesh[i], (v3_t){ 0, 0, 0 }, 0, 0, 0, 1, 0);
#ifdef BENCH_GPU
        if (gpu && gpu3d_flush(&g, 0) != 0) {
            fprintf(stderr, "gpu3d: %s (%s)\n", gpu3d_status(), emu_error);
            return 1;
        }
#endif
        tris = r.tris_in;
        drawn = r.tris_drawn;
        px = r.pixels;
        verts = r.verts;
    }
#ifdef R3D_STATS
    extern uint32_t r3d_stat_visited, r3d_stat_spans, r3d_stat_tris;
    printf("mapbench: gouraud %u triangles, %u spans, %u pixels visited (all frames)\n", r3d_stat_tris, r3d_stat_spans,
           r3d_stat_visited);
#endif
    printf("mapbench: %d models, %u triangles in, %u drawn, %u vertices, %u pixels per frame, %d frames\n", n, tris,
           drawn, verts, px, frames);
    if (argc > 9) {
        FILE *f = fopen(argv[9], "wb");
        fprintf(f, "P6\n320 180\n255\n");
        for (int i = 0; i < 320 * 180; i++) {
            uint16_t p = fb[i];
            uint8_t rgb[3] = { (uint8_t)((p >> 11) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    return 0;
}
