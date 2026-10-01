/*
 * The GPU backend of r3d (src/gpu/gpu3d.c) against the software
 * rasterizer, on the V3D emulator of v3d_emu.c (M30).
 *
 *   test_gpu3d RED_A TEX_SWAP [DIR]
 *
 * RED_A and TEX_SWAP are the emulator's hidden byte orders: the backend's
 * probe must find them. Every scene is drawn by the software and by the
 * GPU; the pictures must match but for edge and texel-border pixels.
 * With DIR, both pictures of every scene go there as PPM.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/r3d.h"
#include "gpu/gpu3d.h"
#include "v3d_emu.h"

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the kernel's printf, for gpu3d.c */
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

static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };
static const char *ppm_dir;

static g16_sheet_t sheet, sheet2, sheet3;
static r3d_mesh_t sphere, quad, floor_m, cube;

/* 128x128: four 32x32 checkers in the top row (red/yellow, blue/white,
 * green/black, grey/orange); the second row: the same checkers whose
 * left column of squares is transparent */
static void make_sheet(void)
{
    static const uint32_t pairs[4][2] = { { 0xE02020, 0xF0E040 }, { 0x2040E0, 0xF0F0F0 },
                                          { 0x20C040, 0x101010 }, { 0x808080, 0xF08020 } };
    g16_sheet_alloc(&sheet, 128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int t = (x / 32) % 4, lx = x % 32, ly = y % 32;
            uint32_t c = pairs[t][((lx / 8) ^ (ly / 8)) & 1];
            int opaque = !(y >= 32 && y < 64 && lx < 8);
            g16_sheet_set(&sheet, x, y, g16_rgb24(c), opaque);
        }
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 16; cx++)
            g16_sheet_update_cell(&sheet, cx, cy);
    /* two more sheets: the same squares in other colours */
    g16_sheet_t *more[2] = { &sheet2, &sheet3 };
    for (int k = 0; k < 2; k++) {
        g16_sheet_alloc(more[k], 128, 128);
        for (int i = 0; i < 128 * 128; i++) {
            uint32_t c = g16_to_rgb24(sheet.px[i]);
            c = k ? (c >> 8 | c << 16) & 0xFFFFFF : ~c & 0xFFFFFF;
            g16_sheet_set(more[k], i % 128, i / 128, g16_rgb24(c), sheet.alpha[i] != 0);
        }
        for (int cy = 0; cy < 16; cy++)
            for (int cx = 0; cx < 16; cx++)
                g16_sheet_update_cell(more[k], cx, cy);
    }
}

static void make_meshes(void)
{
    r3d_mesh_sphere(&sphere, 10, 16, 0x4080FF, 0xFFC040);
    r3d_mesh_cube(&cube, 0x60C060);
    r3d_mesh_t *q[2] = { &quad, &floor_m };
    for (int k = 0; k < 2; k++) {
        r3d_mesh_alloc(q[k], 4, 2);
        q[k]->verts[0] = (v3_t){ -1, -1, 0 }; q[k]->verts[1] = (v3_t){ 1, -1, 0 };
        q[k]->verts[2] = (v3_t){ 1, 1, 0 };   q[k]->verts[3] = (v3_t){ -1, 1, 0 };
        static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
        memcpy(q[k]->faces, f, sizeof f);
        r3d_mesh_alloc_uv(q[k]);
        /* texel v grows downwards: vertex y = +1 is v = 0 */
        const float u0 = k ? 64 : 0, v0 = k ? 0 : 32, s = 31.5f;
        const float uv[12] = { u0, v0 + s, u0 + s, v0, u0 + s, v0 + s,   u0, v0 + s, u0, v0, u0 + s, v0 };
        memcpy(q[k]->uv, uv, sizeof uv);
        q[k]->colors[0] = q[k]->colors[1] = R3D_TEXTURED;
        q[k]->tex = &sheet;
        r3d_mesh_normals(q[k]);
    }
}

/* ---------------------------------------------------------------- scenes */

typedef void (*scene_fn)(r3d_t *r, g16_t *g, int gpu);

static void flush(r3d_t *r, g16_t *g, int gpu)
{
    (void)r;
    if (gpu)
        CHECK(gpu3d_flush(g) == 0, "flush: %s (%s)", gpu3d_status(), emu_error);
}

static void s_spheres(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0, -6, 0, 0, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_fog(r, 0x304050, 6, 9);
    r3d_draw_flags(r, &sphere, (v3_t){ -1.6f, 0.4f, 0 }, 0.3f, 0.5f, 0, 1.2f, 0);
    r3d_draw_flags(r, &sphere, (v3_t){ 1.6f, 0.4f, 1 }, 0.3f, 0.5f, 0, 1.2f, R3D_SMOOTH);
    r3d_draw_flags(r, &cube, (v3_t){ 0, -1.2f, -1 }, 0.4f, 0.7f, 0.2f, 0.8f, 0);
    r3d_fog(r, 0, 0, 0);
    flush(r, g, gpu);
}

/* a textured quad with transparent squares in front of a cube: the cube
 * shows through; a floor under the camera (near plane, guard band) */
static void s_textures(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 1.2f, -4, 0.2f, -0.15f, 70);
    r3d_light(r, 0, 1, -1, 0.5f);
    r3d_draw_flags(r, &floor_m, (v3_t){ 0, -1, 2 }, 1.5707963f, 0, 0, 30, 0);
    r3d_draw_flags(r, &cube, (v3_t){ 0.3f, 0.2f, 2 }, 0.2f, 0.4f, 0, 0.7f, 0);
    r3d_draw_flags(r, &quad, (v3_t){ 0, 0.3f, 0.8f }, 0, 0.3f, 0, 1, 0);
    flush(r, g, gpu);
}

/* a floor without depth drawn first, then objects; then a zclear and an
 * object behind the first ones, which must still be drawn over them */
static void s_noz_zclear(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 2, -6, 0, -0.3f, 60);
    r3d_light(r, 0.3f, 1, -0.4f, 0.4f);
    r3d_draw_flags(r, &floor_m, (v3_t){ 0, -1, 2 }, 1.5707963f, 0, 0, 6, R3D_NOZ);
    r3d_draw_flags(r, &sphere, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1, R3D_SMOOTH);
    r3d_draw_flags(r, &cube, (v3_t){ -1.5f, 0, 1 }, 0, 0.5f, 0, 0.7f, 0);
    r3d_zclear(r);
    r3d_draw_flags(r, &cube, (v3_t){ 0.5f, 0, 4 }, 0, 0.2f, 0, 1.2f, R3D_UNLIT);
    flush(r, g, gpu);
}

/* n small spheres: 330 of them make 45000 triangles, more than a batch
 * (65532 vertices); 700, more than a job */
static void many(r3d_t *r, g16_t *g, int gpu, int n)
{
    r3d_camera(r, 0, 0, -12, 0, 0, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.25f);
    const int side = (int)ceilf(sqrtf((float)n * 1.8f));
    for (int i = 0; i < n; i++) {
        float x = -7.0f + 14.0f * (float)(i % side) / side, y = -3.8f + 7.6f * (float)(i / side) / ((n + side - 1) / side);
        r3d_draw_flags(r, &sphere, (v3_t){ x, y, (float)(i % 3) }, 0.3f * i, 0.5f, 0, 0.3f, i & 1 ? R3D_SMOOTH : 0);
    }
    flush(r, g, gpu);
}

static void s_many(r3d_t *r, g16_t *g, int gpu) { many(r, g, gpu, 330); }

/* three sheets in one job (the GPU keeps two textures): the first one is
 * made again while the job still has triangles that read it */
static void s_sheets(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0, -5, 0, 0, 60);
    r3d_light(r, 0, 0, -1, 0.6f);
    g16_sheet_t *order[4] = { &sheet, &sheet2, &sheet3, &sheet };
    for (int i = 0; i < 4; i++) {
        quad.tex = order[i];
        r3d_draw_flags(r, &quad, (v3_t){ -2.4f + 1.6f * (float)i, i == 3 ? -1.2f : 0.6f, 0 }, 0, 0.2f, 0,
                       0.7f, 0);
    }
    quad.tex = &sheet;
    flush(r, g, gpu);
}
static void s_full(r3d_t *r, g16_t *g, int gpu) { many(r, g, gpu, 700); }

static const struct { const char *name; scene_fn fn; int w, h; float limit; } scenes[] = {
    { "spheres", s_spheres, 640, 360, 0.02f },
    { "textures", s_textures, 640, 360, 0.04f },
    { "noz_zclear", s_noz_zclear, 480, 270, 0.02f },
    { "many", s_many, 640, 360, 0.05f },
    { "3 sheets", s_sheets, 640, 360, 0.02f },
    { "full job", s_full, 640, 360, 0.08f },
};

/* ---------------------------------------------------------------- compare */

static uint32_t rgb(uint16_t c) { return g16_to_rgb24(c); }

static void save(const uint16_t *px, int w, int h, const char *name, const char *kind)
{
    if (!ppm_dir)
        return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s-%s.ppm", ppm_dir, name, kind);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint32_t c = rgb(px[i]);
        fputc((int)(c >> 16), f); fputc((int)(c >> 8 & 255), f); fputc((int)(c & 255), f);
    }
    fclose(f);
}

static void run_scene(int s)
{
    const int w = scenes[s].w, h = scenes[s].h;
    uint16_t *a = test_aligned_alloc(16, (size_t)w * h * 2), *b = test_aligned_alloc(16, (size_t)w * h * 2);
    r3d_t r;
    g16_t g;
    for (int pass = 0; pass < 2; pass++) {
        g16_target(&g, pass ? b : a, (uint32_t)w, w, h, &font);
        g16_cls(&g, g16_rgb(30, 20, 50));
        g16_rectfill(&g, 0, 0, w, 12, g16_rgb(200, 200, 0));      /* drawn by the ARM first */
        r3d_init(&r, &g);
        r.backend = pass ? gpu3d_backend() : NULL;
        r3d_zclear(&r);
        uint32_t prims = emu_stats.prims, batches = emu_stats.batches, jobs = emu_stats.jobs;
        scenes[s].fn(&r, &g, pass);
        if (pass) {
            CHECK(emu_stats.prims > prims, "%s: the GPU drew nothing", scenes[s].name);
            printf("  %-12s %u triangles in %u batches, %u jobs\n", scenes[s].name, emu_stats.prims - prims,
                   emu_stats.batches - batches, emu_stats.jobs - jobs);
            if (s == 3)
                CHECK(emu_stats.batches - batches >= 2, "many: one batch");
            if (s == 4)
                CHECK(emu_stats.jobs - jobs >= 2, "3 sheets: one job");
            if (s == 5)
                CHECK(emu_stats.jobs - jobs >= 2, "full job: one job");
        }
        r3d_free(&r);
    }
    int differ = 0, covered = 0;
    for (int i = 0; i < w * h; i++) {
        uint32_t ca = rgb(a[i]), cb = rgb(b[i]), bg = rgb(g16_rgb(30, 20, 50));
        if (ca != bg) covered++;
        int d = 0;
        for (int k = 0; k < 24; k += 8) {
            int x = (int)(ca >> k & 255) - (int)(cb >> k & 255);
            if (x > 48 || x < -48) d = 1;
        }
        differ += d;
    }
    float frac = (float)differ / (float)(w * h);
    printf("  %-12s %dx%d: %d pixels covered, %.2f%% differ\n", scenes[s].name, w, h, covered, frac * 100);
    CHECK(frac <= scenes[s].limit, "%s: %.2f%% of the pixels differ", scenes[s].name, frac * 100);
    CHECK(rgb(b[5 * w + 5]) == rgb(g16_rgb(200, 200, 0)), "%s: the bar drawn before the 3D is gone",
          scenes[s].name);
    save(a, w, h, scenes[s].name, "cpu");
    save(b, w, h, scenes[s].name, "gpu");
}

int main(int argc, char **argv)
{
    emu_red_a = argc > 1 ? atoi(argv[1]) : 1;
    emu_tex_swap = argc > 2 ? atoi(argv[2]) : 0;
    ppm_dir = argc > 3 ? argv[3] : NULL;
    printf("gpu3d on the emulator: byte a = %s, texels %s\n", emu_red_a ? "red" : "blue",
           emu_tex_swap ? "swapped" : "in place");
    CHECK(gpu3d_init() == 0, "init: %s (%s)", gpu3d_status(), emu_error);
    char want[80];
    snprintf(want, sizeof want, "byte a = %s, texels %s", emu_red_a ? "red" : "blue",
             emu_tex_swap ? "swapped" : "in place");
    CHECK(strstr(gpu3d_status(), want) != NULL, "probe: '%s', expected '%s'", gpu3d_status(), want);
    if (!gpu3d_ready()) {
        printf("gpu3d: %d/%d checks passed\n", checks - failures, checks);
        return 1;
    }
    make_sheet();
    make_meshes();
    for (size_t s = 0; s < sizeof scenes / sizeof *scenes; s++)
        run_scene((int)s);
    gpu3d_stats_t st;
    gpu3d_take_stats(&st);
    CHECK(st.jobs >= 5, "%u jobs", st.jobs);
    printf("gpu3d: %u jobs, %u triangles; %d/%d checks passed\n", st.jobs, st.tris, checks - failures, checks);
    return failures ? 1 : 0;
}
