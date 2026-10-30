/*
 * The heroes of carts/hero (assets/models.bm) on the V3D emulator: with the vertex shader at
 * 2 every hero (and every outline hull) must go through the mesh path (gpu3d_stats glmeshes /
 * gltris), with it off none does. A plain coloured hero with a face on two bones goes back to
 * the ARM, one triangle at a time: the models made before the one-bone-face rule do that, so
 * `test_hero_mesh MODELS.bm --expect-fallback` checks that the old file still shows the fault.
 *
 *   test_hero_mesh carts/hero/assets/models.bm [--expect-fallback]
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/bm.h"
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
static g16_t g;
static r3d_t r;
static float bone_m[32][12];

static int load_model(const bm_cart_t *c, const char *name, r3d_mesh_t *m)
{
    bm_model_t md;
    int found = -1;
    for (int i = 0; bm_mesh_model(c->mesh, c->mesh_size, i, &md) == 0; i++)
        if (strcmp(md.name, name) == 0) {
            found = i;
            break;
        }
    if (found < 0 || r3d_mesh_alloc(m, md.nverts, md.nfaces) != 0)
        return -1;
    for (int i = 0; i < md.nverts; i++) {
        float xyz[3];
        bm_model_vertex(&md, i, xyz);
        m->verts[i] = (v3_t){ xyz[0], xyz[1], xyz[2] };
    }
    for (int f = 0; f < md.nfaces; f++) {
        uint32_t colour;
        float uv[6];
        bm_model_face(&md, f, m->faces + f * 3, &colour, uv);
        m->colors[f] = colour;
    }
    r3d_mesh_normals(m);
    bm_rig_t rig;
    if (bm_anim_rig(c->anim, c->anim_size, name, &rig) != 0 || rig.nverts != md.nverts || rig.nbones > 32)
        return -1;
    m->bones = (const float (*)[12])bone_m;
    m->vbone = rig.vbones;
    m->nbones = rig.nbones;
    return 0;
}

/* a pose: the bones turned a little about z, each its own way */
static void pose(float t)
{
    for (int b = 0; b < 32; b++) {
        const float a = 0.3f * sinf(t + (float)b), cs = cosf(a), sn = sinf(a);
        float *M = bone_m[b];
        memset(M, 0, 12 * sizeof *M);
        M[0] = cs; M[1] = -sn; M[4] = sn; M[5] = cs; M[10] = 1;
    }
}

static int failed;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failed = 1; } } while (0)

/* n models, each drawn twice (smooth light and flat light) */
static gpu3d_stats_t draw_all(r3d_mesh_t *ms, int n, int vs, const char *what)
{
    r.backend = gpu3d_backend();
    gpu3d_set_vshader(vs);
    g16_cls(&g, g16_rgb(10, 10, 30));
    r3d_zclear(&r);
    r3d_camera(&r, 0, 1.5f, -6, 0, 0.9f, 60);
    r3d_light(&r, -0.4f, 0.7f, -0.6f, 0.3f);
    gpu3d_stats_t discard;
    gpu3d_take_stats(&discard);
    pose(0.7f);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 2; k++)
            r3d_draw_flags(&r, &ms[i], (v3_t){ (float)(i - n / 2) * 0.9f, 0, 2.0f + (float)k * 1.5f }, 0, 0.3f * (float)i, 0,
                           1, k ? 0 : R3D_SMOOTH);
    if (gpu3d_flush(&g, 0) != 0) {
        printf("FAIL: gpu3d: %s (%s)\n", gpu3d_status(), emu_error);
        exit(1);
    }
    gpu3d_stats_t s;
    gpu3d_take_stats(&s);
    printf("%-8s vs %d: %2u meshes, %5u mesh tris of %5u tris, %u jobs\n", what, vs, s.glmeshes, s.gltris, s.tris, s.jobs);
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_hero_mesh MODELS.bm [--expect-fallback]\n");
        return 2;
    }
    const int expect_fallback = argc > 2 && strcmp(argv[2], "--expect-fallback") == 0;
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        printf("FAIL: %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)len);
    if (fread(data, 1, (size_t)len, f) != (size_t)len)
        return 1;
    fclose(f);
    static bm_cart_t c;
    char err[96] = "";
    if (bm_parse_any(data, (size_t)len, &c, err, sizeof err) != 0 || !c.mesh || !c.anim) {
        printf("FAIL: %s: %s\n", argv[1], err);
        return 1;
    }
    uint16_t *fb = test_aligned_alloc(64, 1920 * 1080 * 2);
    g16_target(&g, fb, W, W, H, &font);
    r3d_init(&r, &g);
    if (gpu3d_init() != 0) {
        printf("FAIL: gpu3d: %s\n", gpu3d_status());
        return 1;
    }
    static const char *const names[8] = { "hero0", "hero1", "hero2", "hero3", "hero0_o", "hero1_o", "hero2_o", "hero3_o" };
    r3d_mesh_t ms[8];
    for (int i = 0; i < 8; i++)
        CHECK(load_model(&c, names[i], &ms[i]) == 0, "model %s", names[i]);
    if (failed)
        return 1;
    for (int i = 0; i < 8; i++) {
        gpu3d_stats_t off = draw_all(&ms[i], 1, 0, names[i]), on = draw_all(&ms[i], 1, 2, names[i]);
        CHECK(off.glmeshes == 0 && off.gltris == 0, "%s: the mesh path with the vertex shader off", names[i]);
        if (expect_fallback) {
            CHECK(on.glmeshes == 0, "%s: the old model took the mesh path (%u)", names[i], on.glmeshes);
        } else {
            CHECK(on.glmeshes == 2, "%s: %u of 2 draws took the mesh path with vs 2", names[i], on.glmeshes);
            CHECK(on.gltris > 0 && on.gltris <= on.tris, "%s: mesh tris %u of %u", names[i], on.gltris, on.tris);
        }
    }
    /* the crowd: at vs 1 lit heroes go to the ARM */
    gpu3d_stats_t one = draw_all(ms, 4, 1, "crowd");
    CHECK(one.glmeshes == 0, "lit heroes took the mesh path at vs 1 (%u)", one.glmeshes);
    gpu3d_stats_t crowd0 = draw_all(ms, 4, 0, "crowd"), crowd2 = draw_all(ms, 4, 2, "crowd");
    if (!expect_fallback)
        CHECK(crowd2.glmeshes == 8 && crowd2.gltris > crowd0.gltris, "crowd: %u meshes at vs 2", crowd2.glmeshes);
    if (failed)
        return 1;
    printf("hero mesh: ok%s\n", expect_fallback ? " (old models: fallback shown)" : "");
    return 0;
}
