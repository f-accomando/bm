/*
 * Impostors (r3d_t.impostor, r3d_impostor in bm/config.txt, gpu3d()'s
 * fifth): a far small mesh drawn as one quad facing the camera in its
 * average colour. A field of models from near to far, drawn by the ARM and
 * by the GPU backend on the V3D emulator (without and with its vertex
 * shader):
 *   - off: no impostor, and the ARM's picture is the one of before (its
 *     checksum below; tests/bm/bench3d.c keeps the others);
 *   - on: the same meshes become impostors on the ARM and on the GPU (the
 *     choice depends on the camera and the mesh only: lockstep), fewer
 *     triangles, and the picture differs from the full one only a little
 *     (few pixels, and those not far).
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/r3d.h"
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

#define W 640
#define H 360

static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };
static uint16_t *fb;
static g16_t g;
static r3d_t r;
static g16_sheet_t sheet;
static r3d_mesh_t sphere, lamp, box, baked;
static int failed;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failed = 1; } } while (0)

/* the pictures of the scene with the impostors off, as before them (the
 * r3d.c without impostors drew these): the ARM's, the GPU's (with or
 * without its vertex shader) */
#define OFF_ARM_SUM 0x9ac5c23bu
#define OFF_GPU_SUM 0x459ca0feu

static void make_meshes(void)
{
    r3d_mesh_sphere(&sphere, 6, 8, 0x4080FF, 0xFFC040);
    /* a sphere with an emissive half and a glossy one */
    r3d_mesh_sphere(&lamp, 6, 8, 0xFF4020, 0x20FF40);
    for (int t = 0; t < lamp.nfaces; t++)
        lamp.colors[t] |= (t & 1) ? R3D_EMISSIVE : R3D_GLOSSY;
    r3d_mesh_normals(&lamp);
    /* a textured cube (texels of two colours) */
    g16_sheet_alloc(&sheet, 64, 64);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            g16_sheet_set(&sheet, x, y, ((x >> 3) ^ (y >> 3)) & 1 ? g16_rgb(200, 60, 40) : g16_rgb(230, 200, 80), 1);
    for (int cy = 0; cy < 8; cy++)
        for (int cx = 0; cx < 8; cx++)
            g16_sheet_update_cell(&sheet, cx, cy);
    r3d_mesh_cube(&box, 0);
    r3d_mesh_alloc_uv(&box);
    for (int t = 0; t < box.nfaces; t++) {
        box.colors[t] = R3D_TEXTURED;
        static const float uv[2][6] = { { 0, 64, 0, 0, 64, 0 }, { 0, 64, 64, 0, 64, 64 } };
        memcpy(box.uv + t * 6, uv[t & 1], sizeof uv[0]);
    }
    box.tex = &sheet;
    r3d_mesh_normals(&box);
    /* light baked at the corners (the world of a map) */
    r3d_mesh_sphere(&baked, 6, 8, 0xC0C0C0, 0x808080);
    baked.clight = malloc((size_t)baked.nfaces * 9);
    for (int t = 0; t < baked.nfaces * 9; t++)
        baked.clight[t] = (uint8_t)(64 + (t * 37) % 128);
    r3d_mesh_normals(&baked);
}

/* rows of models going away from the camera: near ones whole, far ones a
 * few pixels; the fog and a lamp on */
static void scene(void)
{
    g16_cls(&g, g16_rgb(10, 10, 30));
    r3d_zclear(&r);
    r3d_camera(&r, 0, 1.5f, -6, 0, 0.05f, 60);
    r3d_light(&r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_shine(&r, 0.6f, 16, 0.3f);
    r3d_fog(&r, 0x304050, 60, 160);
    r3d_lamp(&r, 0, 0, 1, 20, 12, 1.0f);
    static r3d_mesh_t *const ms[4] = { &sphere, &lamp, &box, &baked };
    for (int row = 0; row < 16; row++) {
        const float z = 2 + row * row * 0.5f;
        for (int i = 0; i < 15; i++) {
            const int k = (row + i) & 3;
            const float x = (i - 7) * (1.2f + z * 0.07f);
            r3d_draw_flags(&r, ms[k], (v3_t){ x, 0.6f + (i & 1) * 0.4f, z }, 0.3f * i, 0.2f * row, 0,
                           0.5f, k == 0 && (i & 2) ? R3D_SMOOTH : 0);
        }
    }
    r3d_lamp(&r, 0, 0, 0, 0, 0, 0);
    r3d_fog(&r, 0, 0, 0);
}

static uint32_t checksum(const uint16_t *p)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * H; i++) {
        h = (h ^ (p[i] & 0xFF)) * 16777619u;
        h = (h ^ (p[i] >> 8)) * 16777619u;
    }
    return h;
}

typedef struct { uint32_t sum, tris, imps; uint16_t *px; } shot_t;

static shot_t draw(int gpu, int vs, int px)
{
    r.backend = gpu ? gpu3d_backend() : NULL;
    if (gpu)
        gpu3d_set_vshader(vs);
    r.impostor = px;
    scene();
    if (gpu && gpu3d_flush(&g, 0) != 0) {
        printf("FAIL: gpu3d: %s (%s)\n", gpu3d_status(), emu_error);
        exit(1);
    }
    shot_t s = { checksum(fb), r.tris_drawn, r.impostors, malloc(W * H * 2) };
    memcpy(s.px, fb, W * H * 2);
    return s;
}

/* pixels whose colour differs by more than 24 (of 255) in a channel, and
 * the mean difference (0..255) over the picture */
static void compare(const shot_t *a, const shot_t *b, int *far, float *mean)
{
    double sum = 0;
    *far = 0;
    for (int i = 0; i < W * H; i++) {
        const uint32_t x = g16_to_rgb24(a->px[i]), y = g16_to_rgb24(b->px[i]);
        int worst = 0;
        for (int c = 0; c < 24; c += 8) {
            const int d = abs((int)(x >> c & 255) - (int)(y >> c & 255));
            sum += d;
            if (d > worst) worst = d;
        }
        *far += worst > 24;
    }
    *mean = (float)(sum / (3.0 * W * H));
}

static void report(const char *what, const shot_t *off, const shot_t *on, int max_far, float max_mean)
{
    int far;
    float mean;
    compare(off, on, &far, &mean);
    printf("%-22s off %08x %5u tri | on %08x %5u tri, %u impostors | %5d px differ, mean %.3f\n", what, off->sum,
           off->tris, on->sum, on->tris, on->imps, far, mean);
    CHECK(off->imps == 0, "%s: impostors with the option off", what);
    CHECK(on->imps >= 40, "%s: only %u impostors", what, on->imps);
    CHECK(on->tris + 8u * on->imps < off->tris, "%s: not fewer triangles (%u, %u)", what, on->tris, off->tris);
    CHECK(far <= max_far, "%s: %d pixels differ (at most %d)", what, far, max_far);
    CHECK(mean <= max_mean, "%s: mean difference %.3f (at most %.3f)", what, mean, max_mean);
}

int main(void)
{
    fb = test_aligned_alloc(64, W * H * 2);
    g16_target(&g, fb, W, W, H, &font);
    r3d_init(&r, &g);
    make_meshes();
    if (gpu3d_init() != 0) {
        printf("FAIL: gpu3d: %s\n", gpu3d_status());
        return 1;
    }
    /* under 1% of the picture's pixels (an impostor is at most 8x8), the
     * mean far below one step of a colour */
    const int max_far = 2000;
    const float max_mean = 0.25f;

    const shot_t arm_off = draw(0, 0, 0), arm_on = draw(0, 0, 8), arm_on2 = draw(0, 0, 8);
    CHECK(arm_off.sum == OFF_ARM_SUM, "ARM, off: %08x, not the picture of before (%08x)", arm_off.sum, OFF_ARM_SUM);
    CHECK(arm_on.sum == arm_on2.sum, "ARM, on: two draws differ");
    report("ARM", &arm_off, &arm_on, max_far, max_mean);
    const shot_t arm_on6 = draw(0, 0, 6);
    CHECK(arm_on6.imps < arm_on.imps && arm_on6.imps > 0, "ARM: 6 px gives %u impostors, 8 px %u", arm_on6.imps,
          arm_on.imps);
    printf("ARM, 6 px: %u impostors, %u tri\n", arm_on6.imps, arm_on6.tris);

    for (int vs = 0; vs <= 2; vs += 2) {
        const shot_t off = draw(1, vs, 0), on = draw(1, vs, 8);
        char what[32];
        snprintf(what, sizeof what, "GPU%s", vs ? "+VS" : "");
        CHECK(off.sum == OFF_GPU_SUM, "%s, off: %08x, not the picture of before (%08x)", what, off.sum, OFF_GPU_SUM);
        report(what, &off, &on, max_far, max_mean);
        CHECK(on.imps == arm_on.imps, "%s: %u impostors, the ARM %u (not the same choice)", what, on.imps,
              arm_on.imps);
        /* the impostors themselves: the GPU's against the ARM's */
        int far;
        float mean;
        compare(&arm_on, &on, &far, &mean);
        int far0;
        float mean0;
        compare(&arm_off, &off, &far0, &mean0);
        printf("%-22s GPU against ARM: off %d px, mean %.3f; on %d px, mean %.3f\n", what, far0, mean0, far, mean);
        CHECK(far <= far0 + max_far / 4, "%s: the impostors differ from the ARM's (%d px, %d without)", what, far,
              far0);
        free(off.px);
        free(on.px);
    }
    if (failed)
        return 1;
    printf("impostors: ok\n");
    return 0;
}
