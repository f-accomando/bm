/*
 * The 3D Bench (b3d.h, docs/BENCH3D.md).
 *
 * Tests: scenes whose load n grows (spheres, heroes, screens of pixels,
 * draws...). Profiles: the renderers, each reproducing a version of the
 * drivers (src/gpu/version3d.h): the ARM (0.2), the GPU (2.1), with MSAA,
 * with the vertex shader for the scenery (3.0) and for every model (3.4).
 * For each test and profile n grows by about a third until a frame takes
 * more than LIMIT_MS (a step's time: the middle of its 6 frames); the loads
 * at 60 and 30 fps are interpolated from the last step under them (a slow
 * step before a heavier one that fits was slowed by something else), and
 * the work of the last step under 60 fps is kept for the report.
 * The score (the first page): each test's load at 60 fps with the driver
 * as the games get it against bm3d 2.1 on the Pi, a geometric mean times
 * 1000; and the triangles a frame at 60 fps of a scene with everything
 * at once (the mix).
 *
 * The sphere and quad scenes are those of the stress test (src/bm/
 * stress.c), at the same 640x360: the numbers the Pi gave with the drivers
 * before (docs/GPU-BENCHMARK-REPORT.md) are their bars of history. The loads go
 * far beyond what the drivers do today, to leave room for the next ones.
 */
#include "b3d.h"
#include "bm/r3d.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "lib/printf.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 360
#define LIMIT_MS 40.0f
#define MS60 16.667f
#define MS30 33.333f
#define RAMP_US 25000000u               /* a ramp stops after 25 s */

static const b3d_platform_t *P;

static const char *pmu_name(void)
{
    return P->pmu ? P->pmu : "ARM1176 PMU";
}

static r3d_t R;
static g16_t *g;

/* ---------------------------------------------------------------- profiles */

enum { PF_ARM, PF_GPU, PF_AA, PF_VS1, PF_VS, PF_VSQ, PF_Q, PF_WC, PF_2D, PF_VSQ2, PF_T16, PF_FS2, PF_VSS, PF_VSF,
       PF_VSA, PF_ETC1, PF_VSAQ, PF_QPU, NPROF };
#define M_ARM (1u << PF_ARM)
#define M_GPU (1u << PF_GPU)
#define M_AA  (1u << PF_AA)
#define M_VS1 (1u << PF_VS1)
#define M_VS  (1u << PF_VS)
#define M_VSQ (1u << PF_VSQ)
#define M_Q   (1u << PF_Q)              /* the queue without the vertex shader (bm3d 4.4) */
#define M_WC  (1u << PF_WC)             /* the GPU with the jobs' memory uncached (bm3d 4.5) */
#define M_2D  (1u << PF_2D)             /* the GPU with the 2D over the 3D in its job (bm3d 4.8) */
#define M_VSQ2 (1u << PF_VSQ2)          /* GPU+VS+Q with two jobs in flight (bm3d 5.1) */
#define M_T16 (1u << PF_T16)            /* the GPU with opaque textures in 16 bits (bm3d 5.2) */
#define M_FS2 (1u << PF_FS2)            /* the GPU with two-thread pixel shaders (bm3d 5.3; every textured one 6.3) */
#define M_VSS (1u << PF_VSS)            /* GPU+VS with the meshes nearest first (bm3d 5.4) */
#define M_VSA (1u << PF_VSA)            /* GPU+VS+FS2 with compact attributes (bm3d 7.1) */
#define M_VSAQ (1u << PF_VSAQ)          /* GPU+VS+A8 with the frame in the queue (herotris: docs/design/triangle-throughput.md) */
#define M_ETC1 (1u << PF_ETC1)          /* GPU+T16 with the opaque sheets as ETC1 (bm3d 7.3) */
#define M_QPU (1u << PF_QPU)            /* the ARM with the full-screen fade on the QPUs (gpu3d_qpu=1; the fade test only) */
#define M_ALL (M_ARM | M_GPU | M_VS1 | M_VS)
#define M_LIT (M_ARM | M_GPU | M_VS)    /* models lit by the sun: VS1 is the GPU for them */

/* queue (bm3d 4.1): the end of each frame started on the GPU, the ARM's work of
 * the frame (the test's `work`) done meanwhile */
/* attr (bm3d 7.1): compact attributes (gpu3d_attr), on GPU+VS+FS2: GPU+VS+A8 */
/* etc1 (bm3d 7.3): the opaque sheets as ETC1 (gpu3d_etc1), on GPU+T16
 * (the cut-outs stay RGBA5551): GPU+ETC1 against GPU+T16 is ETC1 alone */
static const struct { const char *name; int gpu, aa, vs, queue, wc, two_d, t16, fs2, sort, attr, etc1; } prof[NPROF] = {
    { "ARM", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "GPU", 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "GPU+AA", 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "GPU+VS1", 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { "GPU+VS", 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "GPU+VS+Q", 1, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0 }, { "GPU+Q", 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
    { "GPU+WC", 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, { "GPU+2D", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
    { "GPU+VS+Q2", 1, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0 }, { "GPU+T16", 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
    { "GPU+FS2", 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 }, { "GPU+VS+S", 1, 0, 2, 0, 0, 0, 0, 0, 1, 0, 0 },
    { "GPU+VS+FS2", 1, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0 }, { "GPU+VS+A8", 1, 0, 2, 0, 0, 0, 0, 1, 0, 1, 0 },
    { "GPU+ETC1", 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1 }, { "GPU+VS+A8+Q", 1, 0, 2, 1, 0, 0, 0, 1, 0, 1, 0 },
    { "QPU", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static const char *prof_version(int p)
{
    return prof[p].etc1 ? "7.3" : prof[p].attr ? "7.1" : prof[p].vs && prof[p].fs2 ? "6.6" : prof[p].sort ? "5.4" : prof[p].fs2 ? "5.3" : prof[p].t16 ? "5.2" : prof[p].queue == 2 ? "5.1" : prof[p].two_d ? "4.8" : prof[p].wc ? "4.5"
         : bm3d_mode_q(prof[p].gpu, prof[p].vs, prof[p].queue);
}

/* bm3d 4.8: the profile draws the 2D over the 3D on the GPU, in its job */
static int cur_2d;
static int keep2d;                      /* the scene draws 3D after its 2D (the depth is kept) */

/* 2D over the 3D: into the GPU's job (GPU+2D), or by the ARM after the
 * 3D so far (the job ends) */
static void flush2d(void)
{
    if (R.backend && gpu3d_pending())
        gpu3d_flush(g, keep2d);
}

/* the pixels a frame asks to fill (quads, 2D rectangles, sprites, text):
 * the per-pixel costs of the tests that measure them (test_t px) */
static float fill_px;

static void rect2d(int x, int y, int w, int h, uint16_t c)
{
    fill_px += (float)w * (float)h;
    if (cur_2d && R.backend && gpu3d_rect2d(g, x, y, x + w, y + h, c))
        return;
    flush2d();
    g16_rectfill(g, x, y, w, h, c);
}

static void text2d(int x, int y, const char *t, uint16_t c)
{
    if (g->font)
        fill_px += (float)strlen(t) * (float)g->font->width * (float)g->font->height;
    if (cur_2d && R.backend && gpu3d_text2d(g, x, y, t, c, 1))
        return;
    flush2d();
    g16_text(g, x, y, t, c);
}

static void spr2d(const g16_sheet_t *s, int sx, int sy, int sw, int sh, int x, int y)
{
    fill_px += (float)sw * (float)sh;
    if (cur_2d && R.backend && gpu3d_blit2d(g, s, sx, sy, sw, sh, x, y, 1, 0, 0))
        return;
    flush2d();
    g16_sspr(g, s, sx, sy, sw, sh, x, y, 0, 0);
}

/* ---------------------------------------------------------------- meshes */

static r3d_mesh_t sphere, quad, cube, tile, grid, hero, hero_tex, hero_skin;
static g16_sheet_t sheet[3];
static float bones[16][12];
static uint8_t vbone[16 * 64], vbone_skin[16 * 64];
static unsigned flags;                  /* r3d flags of the spheres and quads */

/* 128x128 checkers in two colours, one sheet a variant (k) */
static void make_sheet_wh(g16_sheet_t *s, int w, int h, int k, int holes)
{
    static const uint32_t pairs[3][2] = { { 0x2060D0, 0xF0D040 }, { 0xD04030, 0xF0F0F0 }, { 0x30A050, 0x202020 } };
    g16_sheet_alloc(s, w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint32_t c = pairs[k][((x >> 3) ^ (y >> 3)) & 1];
            g16_sheet_set(s, x, y, g16_rgb24(c), !(holes && ((x >> 2) & 3) == 0 && ((y >> 2) & 3) == 0));
        }
    for (int cy = 0; cy < h / 8; cy++)
        for (int cx = 0; cx < w / 8; cx++)
            g16_sheet_update_cell(s, cx, cy);
}

static void make_sheet(g16_sheet_t *s, int k, int holes)
{
    make_sheet_wh(s, 128, 128, k, holes);
}

/* baked light at every corner (a "lit" model of a map) */
static void bake(r3d_mesh_t *m, int base)
{
    m->clight = malloc((size_t)m->nfaces * 9);
    if (!m->clight)
        return;
    for (int i = 0; i < m->nfaces * 9; i++)
        m->clight[i] = (uint8_t)(base + (i * 37) % 90);
}

static void textured(r3d_mesh_t *m, const g16_sheet_t *s)
{
    r3d_mesh_alloc_uv(m);
    for (int t = 0; t < m->nfaces; t++) {
        m->colors[t] = R3D_TEXTURED;
        for (int i = 0; i < 3; i++) {
            const int v = m->faces[t * 3 + i];
            m->uv[t * 6 + i * 2] = (float)(v * 13 % 8) * 16.0f;
            m->uv[t * 6 + i * 2 + 1] = (float)(v * 7 % 8) * 16.0f;
        }
    }
    m->tex = s;
}

/* a quad of 2 x 1.125 units (16:9) in the plane z = 0, facing -z */
static void make_quad(r3d_mesh_t *m, uint32_t colour, const g16_sheet_t *s)
{
    r3d_mesh_alloc(m, 4, 2);
    m->verts[0] = (v3_t){ -1, -0.5625f, 0 }; m->verts[1] = (v3_t){ 1, -0.5625f, 0 };
    m->verts[2] = (v3_t){ 1, 0.5625f, 0 };   m->verts[3] = (v3_t){ -1, 0.5625f, 0 };
    static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
    memcpy(m->faces, f, sizeof f);
    m->colors[0] = m->colors[1] = colour;
    if (s) {
        r3d_mesh_alloc_uv(m);
        static const float uv[12] = { 0, 128, 128, 0, 128, 128,   0, 128, 0, 0, 128, 0 };
        memcpy(m->uv, uv, sizeof uv);
        m->colors[0] = m->colors[1] = R3D_TEXTURED;
        m->tex = s;
    }
    r3d_mesh_normals(m);
}

/* a flat tile of k x k squares, side 1, in the plane y = 0, facing up */
static void make_tile(r3d_mesh_t *m, int k, uint32_t colour)
{
    r3d_mesh_alloc(m, (k + 1) * (k + 1), 2 * k * k);
    for (int z = 0; z <= k; z++)
        for (int x = 0; x <= k; x++)
            m->verts[z * (k + 1) + x] = (v3_t){ (float)x / k - 0.5f, 0, (float)z / k - 0.5f };
    int t = 0;
    for (int z = 0; z < k; z++)
        for (int x = 0; x < k; x++, t += 2) {
            const uint16_t a = (uint16_t)(z * (k + 1) + x), b = (uint16_t)(a + 1), c = (uint16_t)(a + k + 1),
                           d = (uint16_t)(c + 1);
            const uint16_t f[6] = { a, c, b, b, c, d };     /* clockwise seen from above */
            memcpy(m->faces + t * 3, f, sizeof f);
            m->colors[t] = m->colors[t + 1] = colour;
        }
    r3d_mesh_normals(m);
}

/* a hero: 16 spheres of 96 faces (body parts), each on its bone */
static const float part[16][4] = {     /* x y z of the part's middle, its size */
    { 0, 1.0f, 0, 0.32f }, { 0, 1.45f, 0, 0.30f }, { 0, 1.85f, 0, 0.18f }, { 0, 0.62f, 0, 0.24f },
    { -0.35f, 1.5f, 0, 0.12f }, { -0.42f, 1.2f, 0, 0.11f }, { -0.45f, 0.95f, 0, 0.09f },
    { 0.35f, 1.5f, 0, 0.12f }, { 0.42f, 1.2f, 0, 0.11f }, { 0.45f, 0.95f, 0, 0.09f },
    { -0.15f, 0.5f, 0, 0.13f }, { -0.15f, 0.25f, 0, 0.12f }, { -0.15f, 0.05f, 0.05f, 0.08f },
    { 0.15f, 0.5f, 0, 0.13f }, { 0.15f, 0.25f, 0, 0.12f }, { 0.15f, 0.05f, 0.05f, 0.08f },
};

/* tex: every face textured (the Meshy heroes of Overbit), the light of the
 * sun at the corners */
static void make_hero(r3d_mesh_t *m, const g16_sheet_t *tex)
{
    r3d_mesh_t s;
    r3d_mesh_sphere(&s, 6, 8, 0xC05040, 0x4070C0);
    const int sv = s.nverts < 64 ? s.nverts : 64;
    r3d_mesh_alloc(m, 16 * sv, 16 * s.nfaces);
    if (tex)
        r3d_mesh_alloc_uv(m);
    for (int b = 0; b < 16; b++) {
        for (int i = 0; i < sv; i++) {
            const v3_t v = s.verts[i];
            m->verts[b * sv + i] = (v3_t){ part[b][0] + v.x * part[b][3], part[b][1] + v.y * part[b][3],
                                           part[b][2] + v.z * part[b][3] };
            vbone[b * sv + i] = (uint8_t)b;
        }
        for (int t = 0; t < s.nfaces; t++) {
            for (int k = 0; k < 3; k++)
                m->faces[(b * s.nfaces + t) * 3 + k] = (uint16_t)(b * sv + s.faces[t * 3 + k]);
            const int f = b * s.nfaces + t;
            m->colors[f] = s.colors[t] | (b < 3 && t % 2 ? R3D_GLOSSY : 0) | (b == 2 && t < 8 ? R3D_EMISSIVE : 0);
            if (tex) {
                /* a part of the sheet a bone, its face's corners from the sphere's x and y */
                m->colors[f] = R3D_TEXTURED | (b == 2 && t < 8 ? R3D_EMISSIVE : 0);
                for (int k = 0; k < 3; k++) {
                    const v3_t v = s.verts[s.faces[t * 3 + k]];
                    m->uv[f * 6 + k * 2] = (float)(b % 4) * 32.0f + 16.0f + 15.0f * v.x;
                    m->uv[f * 6 + k * 2 + 1] = (float)(b / 4) * 32.0f + 16.0f - 15.0f * v.y;
                }
            }
        }
    }
    m->tex = tex;
    m->bones = (const float (*)[12])bones;
    m->vbone = vbone;
    m->nbones = 16;
    r3d_mesh_normals(m);
    r3d_mesh_free(&s);
}

/* the bones of a hero at time t: each part turns about its middle */
static void pose(float t)
{
    for (int b = 0; b < 16; b++) {
        const float a = (b ? 0.35f : 0.1f) * sinf(t * 3.0f + (float)b);
        const float c = cosf(a), s = sinf(a), *m = part[b];
        /* rotation about x through the part's middle: x' = R (x - m) + m */
        const float B[12] = { 1, 0, 0, 0,
                              0, c, -s, m[1] - (c * m[1] - s * m[2]),
                              0, s, c, m[2] - (s * m[1] + c * m[2]) };
        memcpy(bones[b], B, sizeof B);
    }
}

static void meshes_make(void)
{
    make_sheet(&sheet[0], 0, 0);
    make_sheet(&sheet[1], 1, 1);
    make_sheet(&sheet[2], 2, 0);
    r3d_mesh_sphere(&sphere, 6, 8, 0x4080FF, 0xFFC040);       /* 96 faces, as the stress test */
    r3d_mesh_cube(&cube, 0x60C060);
    make_tile(&tile, 4, 0x80A060);                            /* 32 faces, a piece of map */
    textured(&tile, &sheet[0]);
    bake(&tile, 120);
    make_tile(&grid, 16, 0xD0D0D0);                           /* 512 small faces */
    make_hero(&hero, NULL);
    make_hero(&hero_tex, &sheet[0]);
    /* a skin: the same, the upper half of each part on the next bone (a
     * face round the middle of a part on two bones, as the Meshy heroes) */
    make_hero(&hero_skin, &sheet[0]);
    for (int i = 0; i < hero_skin.nverts; i++) {
        const int b = vbone[i];
        vbone_skin[i] = (uint8_t)(hero_skin.verts[i].y > part[b][1] + 0.02f ? (b + 1) % 16 : b);
    }
    hero_skin.vbone = vbone_skin;
}

static void meshes_free(void)
{
    r3d_mesh_free(&sphere);
    r3d_mesh_free(&quad);
    r3d_mesh_free(&cube);
    r3d_mesh_free(&tile);
    r3d_mesh_free(&grid);
    r3d_mesh_free(&hero);
    r3d_mesh_free(&hero_tex);
    r3d_mesh_free(&hero_skin);
    for (int i = 0; i < 3; i++)
        g16_sheet_free(&sheet[i]);
}

/* ---------------------------------------------------------------- scenes */

static void cls3d(void)
{
    g16_cls(g, g16_rgb(10, 10, 30));
    if (R.backend)
        gpu3d_page(1, g16_rgb(10, 10, 30));
    r3d_zclear(&R);
}

static void scene_light(void)
{
    r3d_light(&R, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_sky(&R, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
    r3d_shine(&R, 0.6f, 16, 0);
    r3d_fog(&R, 0, 0, 0);
    for (int i = 0; i < R3D_LAMPS; i++)
        r3d_lamp(&R, i, 0, 0, 0, 0, 0);
}

/* n spheres of 96 faces on a grid in front of the camera (the stress test's) */
static void spheres(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 0, -8, 0, 0, 60);
    const int side = (int)ceilf(sqrtf((float)n));
    const float step = 9.0f / (float)(side > 1 ? side : 1);
    for (int i = 0; i < n; i++) {
        const float x = -4.5f + step * ((float)(i % side) + 0.5f), y = 2.6f - step * 0.56f * ((float)(i / side) + 0.5f);
        r3d_draw_flags(&R, &sphere, (v3_t){ x, y, (float)(i % 3) }, (float)f * 0.03f + (float)i, (float)f * 0.05f, 0,
                       step * 0.45f, flags);
    }
}

static void sp_flat(void) { flags = 0; }
static void sp_smooth(void) { flags = R3D_SMOOTH; }
static void sp_unlit(void) { flags = R3D_UNLIT; }
static void sp_tex(void) { flags = 0; textured(&sphere, &sheet[0]); }
static void sp_baked(void) { flags = 0; bake(&sphere, 100); r3d_mesh_normals(&sphere); }
static void sp_shine(void)
{
    flags = R3D_SMOOTH;
    for (int t = 0; t < sphere.nfaces; t++)
        sphere.colors[t] |= R3D_GLOSSY;
    r3d_mesh_normals(&sphere);
}

static void shine_light(void)
{
    r3d_sky(&R, 0xFFF0D0, 0x90B0FF, 0x806040);
    r3d_shine(&R, 0.6f, 16, 0.5f);
    r3d_fog(&R, 0x9090A0, 6, 14);
    for (int i = 0; i < R3D_LAMPS; i++)
        r3d_lamp_rgb(&R, i, -3.0f + 2.0f * (float)i, 0.5f, -1, 2.5f, 0.8f, 0xFF8040 >> (i * 4));
}

static void spheres_shine(int n, int f)
{
    shine_light();
    spheres(n, f);
    scene_light();
}

/* the sphere back as made */
/* bm3d 5.0: big meshes: a model of 10 080 triangles (a smooth sphere, 5112
 * vertices: more than the 4096 of a mesh before bm3d 5.0) and a "lit" map
 * of 14 112 (a textured grid, its light baked at each vertex, the same for
 * every face on it, as a real map's) */
static r3d_mesh_t bigm, bigmap;

static void big_setup(void)
{
    r3d_mesh_sphere(&bigm, 70, 72, 0x4080FF, 0xFFC040);
    make_tile(&bigmap, 84, 0x80A060);
    textured(&bigmap, &sheet[0]);
    bigmap.clight = malloc((size_t)bigmap.nfaces * 9);
    if (bigmap.clight)
        for (int t = 0; t < bigmap.nfaces; t++)
            for (int k = 0; k < 3; k++) {
                const int v = bigmap.faces[t * 3 + k];
                for (int c = 0; c < 3; c++)
                    bigmap.clight[t * 9 + k * 3 + c] = (uint8_t)(100 + (v * 37 + c * 11) % 80);
            }
    r3d_mesh_normals(&bigmap);
}

static void big_free(void)
{
    r3d_mesh_free(&bigm);
    r3d_mesh_free(&bigmap);
}

/* the map under n big models (2 is the question: "34 000 triangles?") */
static void big(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 3, -9, 0, -0.25f, 60);
    scene_light();
    r3d_draw_flags(&R, &bigmap, (v3_t){ 0, -1.2f, 6 }, 0, 0.1f, 0, 24, 0);
    for (int i = 0; i < n; i++)
        r3d_draw_flags(&R, &bigm, (v3_t){ -3.0f + 2.0f * (float)(i % 4), 0.4f * (float)(i / 4), (float)(i / 4) * 1.5f },
                       (float)f * 0.02f, (float)f * 0.03f + (float)i, 0, 1.0f, R3D_SMOOTH);
}

static void sp_undo(void)
{
    r3d_mesh_free(&sphere);
    r3d_mesh_sphere(&sphere, 6, 8, 0x4080FF, 0xFFC040);
    flags = 0;
}

/* quads of 320x180 pixels, one per quadrant in turn, each nearer than
 * the one before (the stress test's): every pixel drawn */
static void quads(int n, int f)
{
    (void)f;
    cls3d();
    r3d_camera(&R, 0, 0, 0, 0, 0, 60);
    r3d_light(&R, 0.3f, 0.4f, -1, 0.3f);
    const float k = 160.0f / R.focal;
    const float step = R.backend ? 0.9985f : 0.985f;
    float d = 40;
    for (int i = 0; i < n; i++, d *= step) {
        const float x = (i & 1) ? k * d : -k * d, y = (i & 2) ? -0.5625f * k * d : 0.5625f * k * d;
        r3d_draw_flags(&R, &quad, (v3_t){ x, y, d }, 0, 0, 0, k * d, flags);
        fill_px += 320.0f * 180.0f;
    }
    scene_light();
}

static void q_flat(void) { flags = 0; make_quad(&quad, 0x80C0FF, NULL); }
static void q_smooth(void) { flags = R3D_SMOOTH; make_quad(&quad, 0x80C0FF, NULL); }
static void q_tex(void) { flags = 0; make_quad(&quad, 0, &sheet[0]); }
static void q_alpha(void) { flags = 0; make_quad(&quad, 0, &sheet[1]); }
static void q_screen(void) { flags = 0; make_quad(&quad, 0x80C0FF | R3D_SCREEN, NULL); }
/* M34: textured screen-door faces (on the ARM before bm3d 4.7, a frame each) */
static void q_texscreen(void)
{
    flags = 0;
    make_quad(&quad, 0, &sheet[1]);
    quad.colors[0] = quad.colors[1] = R3D_TEXTURED | R3D_SCREEN;
}
static void q_free(void) { r3d_mesh_free(&quad); flags = 0; }
/* bm3d 4.8: textured quads filtered by the TMU (bilinear: four texels a pixel) */
static void q_bilinear(void) { q_tex(); gpu3d_set_bilinear(1); }
static void q_bilinear_free(void) { q_free(); gpu3d_set_bilinear(0); }

/* n grids of 512 small faces (about 4 pixels each) */
static void tiny(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 6, -6, 0, -0.7f, 60);
    const int side = (int)ceilf(sqrtf((float)n));
    for (int i = 0; i < n; i++) {
        const float x = -5.0f + 10.0f * ((float)(i % side) + 0.5f) / (float)side,
                    z = -2.0f + 8.0f * ((float)(i / side) + 0.5f) / (float)side;
        r3d_draw_flags(&R, &grid, (v3_t){ x, 0, z }, 0, (float)f * 0.01f, 0, 0.35f, 0);
    }
}

/* n small cubes, each its own draw: what a draw costs */
static void draws(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 0, -8, 0, 0, 60);
    const int side = (int)ceilf(sqrtf((float)n));
    for (int i = 0; i < n; i++) {
        const float x = -5.0f + 10.0f * ((float)(i % side) + 0.5f) / (float)side,
                    y = 3.0f - 6.0f * ((float)(i / side) + 0.5f) / (float)side;
        r3d_draw_flags(&R, &cube, (v3_t){ x, y, 0 }, (float)i, (float)f * 0.05f, 0, 2.5f / (float)side, 0);
    }
}

/* heroes on a ring of rows, their bones moving; with their shadows on a
 * floor (sh), or the floor alone */
static const r3d_mesh_t *hero_m = &hero;

static void heroes_at(int n, int f, int sh, int floor_k)
{
    for (int i = 0; i < floor_k; i++)
        for (int j = 0; j < floor_k; j++)
            r3d_draw_flags(&R, &tile, (v3_t){ -12.0f + 6.0f * (float)i, 0, -2.0f + 6.0f * (float)j }, 0, 0, 0, 6, 0);
    const int row = 8;
    for (int i = 0; i < n; i++) {
        const float x = -4.2f + 1.2f * (float)(i % row) + 0.6f * (float)((i / row) % 2),
                    z = 1.0f + 1.5f * (float)(i / row);
        pose((float)f * 0.05f + (float)i);
        if (sh)
            r3d_draw_flags(&R, hero_m, (v3_t){ x, 0, z }, 0, (float)i, 0, 1, R3D_SHADOW);
        r3d_draw_flags(&R, hero_m, (v3_t){ x, 0, z }, 0, (float)i, 0, 1, R3D_SMOOTH);
    }
}

static void heroes(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 2.2f, -4, 0, -0.18f, 60);
    shine_light();
    heroes_at(n, f, 0, 0);
    scene_light();
}

/* the same textured, as the Meshy heroes of Overbit */
static void heroes_tex(int n, int f)
{
    hero_m = &hero_tex;
    heroes(n, f);
    hero_m = &hero;
}

/* and as skins: faces on two bones */
static void heroes_skin(int n, int f)
{
    hero_m = &hero_skin;
    heroes(n, f);
    hero_m = &hero;
}

static void heroes_shadow(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 2.2f, -4, 0, -0.18f, 60);
    shine_light();
    R.shadow_style = 1;
    heroes_at(n, f, 1, 4);
    scene_light();
}

/* n pieces of map (32 faces, baked light, textured) around and under the
 * camera: the nearest through the near plane (the GPU clips them) */
static void clip_scene(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 1.6f, 0, (float)f * 0.01f, -0.25f, 70);
    int k = 0;
    for (int ring = 0; k < n; ring++)
        for (int i = -ring; i <= ring && k < n; i++)
            for (int j = -ring; j <= ring && k < n; j++) {
                if (ring && abs(i) != ring && abs(j) != ring)
                    continue;
                r3d_draw_flags(&R, &tile, (v3_t){ 4.0f * (float)i, 0, 4.0f * (float)j }, 0, 0, 0, 4, 0);
                if (k % 4 == 3)         /* a wall too */
                    r3d_draw_flags(&R, &tile, (v3_t){ 4.0f * (float)i, 2, 4.0f * (float)j + 2 }, 1.5707963f, 0, 0,
                                   4, 0);
                k++;
            }
}

/* n rounds of 3D then 2D over it: the GPU's job ends at each (its depth
 * kept), as a game's HUD between 3D parts */
static void split(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 0, -8, 0, 0, 60);
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 4; k++)
            r3d_draw_flags(&R, &sphere, (v3_t){ -4.0f + (float)((i * 4 + k) % 9), 2.0f - (float)((i * 4 + k) / 9 % 5), 1 },
                           (float)f * 0.03f + (float)k, 0, 0, 0.6f, 0);
        keep2d = 1;
        rect2d((i * 37) % (W - 40), (i * 23) % (H - 10), 40, 10, g16_rgb(200, 200, 40));
        keep2d = 0;
    }
}

/* bm3d 4.8: n sprites of 16 x 16 and n / 4 lines of text over a few spheres:
 * a HUD, a radar of a game; with GPU+2D in the job, else the ARM after it */
static void gpu2d_scene(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 0, -8, 0, 0, 60);
    for (int k = 0; k < 6; k++)
        r3d_draw_flags(&R, &sphere, (v3_t){ -4.0f + 1.6f * (float)k, 0.5f * (float)(k % 2), 1 },
                       (float)f * 0.03f + (float)k, 0, 0, 0.8f, 0);
    for (int i = 0; i < n; i++)
        spr2d(&sheet[1], (i % 8) * 16, (i / 8 % 8) * 16, 16, 16, (i * 53 + f) % (W - 16), (i * 29) % (H - 16));
    for (int i = 0; i < n / 4; i++)
        text2d((i * 71) % (W - 96), (i * 17) % (H - 16), "SCORE 12345", 0xFFE0);
}

/* textured quads on three sheets in turn (the GPU keeps two) */
static void texswap(int n, int f)
{
    (void)f;
    cls3d();
    r3d_camera(&R, 0, 0, -8, 0, 0, 60);
    const int side = (int)ceilf(sqrtf((float)n));
    for (int i = 0; i < n; i++) {
        quad.tex = &sheet[i % 3];
        const float x = -5.0f + 10.0f * ((float)(i % side) + 0.5f) / (float)side,
                    y = 3.0f - 6.0f * ((float)(i / side) + 0.5f) / (float)side;
        r3d_draw_flags(&R, &quad, (v3_t){ x, y, 0 }, 0, 0, 0, 4.0f / (float)side, 0);
    }
    quad.tex = &sheet[0];
}

/* a match: a map of 10 x 10 pieces, n heroes with their shadows, a model
 * in first person, a HUD drawn over the 3D */
static void match(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0.3f * sinf((float)f * 0.02f), 1.7f, -5, 0.05f, -0.12f, 70);
    shine_light();
    R.shadow_style = 1;
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++)
            r3d_draw_flags(&R, &tile, (v3_t){ -27.0f + 6.0f * (float)i, 0, -6.0f + 6.0f * (float)j }, 0, 0, 0, 6, 0);
    heroes_at(n, f, 1, 0);
    pose((float)f * 0.05f);
    r3d_draw_flags(&R, &hero, (v3_t){ 0.5f, 0.2f, -4.2f }, 0, 3.0f, 0, 0.5f, R3D_SMOOTH | R3D_FRONT);
    scene_light();
    rect2d(8, H - 30, 180, 20, g16_rgb(20, 30, 40));
    rect2d(W - 120, H - 30, 110, 20, g16_rgb(20, 30, 40));
    text2d(12, H - 28, "HUD 250/250", 0xFFFF);
}

/* everything a game uses at once, n slices of a street: under each hero
 * (in turn plain with gloss, textured, a textured skin) a piece of lit map,
 * its shadow, a fence of texels with holes, a pane of screen-door glass and
 * a crate; the ground under the camera through the near plane, the sky,
 * rim, gloss, 4 lamps and fog, a model in first person, a HUD of sprites
 * and text over the 3D. Its triangles at 60 fps are the score's "how many
 * in a frame" */
static r3d_mesh_t patch, fence, glass;

static void mix_setup(void)
{
    make_tile(&patch, 4, 0x80A060);
    for (int i = 0; i < patch.nverts; i++) {
        patch.verts[i].x *= 1.2f;       /* the heroes' rows: 1.2 apart, 1.5 between rows */
        patch.verts[i].z *= 1.5f;
    }
    textured(&patch, &sheet[0]);
    bake(&patch, 120);
    make_quad(&fence, 0, &sheet[1]);
    make_quad(&glass, 0x80C0FF | R3D_SCREEN, NULL);
}

static void mix_free(void)
{
    r3d_mesh_free(&patch);
    r3d_mesh_free(&fence);
    r3d_mesh_free(&glass);
}

static void mix(int n, int f)
{
    static const r3d_mesh_t *const kind[3] = { &hero, &hero_tex, &hero_skin };
    cls3d();
    r3d_camera(&R, 0.3f * sinf((float)f * 0.02f), 1.7f, -5, 0.05f, -0.12f, 70);
    shine_light();
    R.shadow_style = 1;
    for (int k = 0; k < 2; k++)
        r3d_draw_flags(&R, &tile, (v3_t){ k ? 3.0f : -3.0f, 0, -4 }, 0, 0, 0, 6, 0);
    const int row = 8;
    for (int i = 0; i < n; i++) {
        const float x = -4.2f + 1.2f * (float)(i % row) + 0.6f * (float)((i / row) % 2),
                    z = 1.0f + 1.5f * (float)(i / row);
        r3d_draw_flags(&R, &patch, (v3_t){ x, 0, z }, 0, 0, 0, 1, 0);
        pose((float)f * 0.05f + (float)i);
        r3d_draw_flags(&R, kind[i % 3], (v3_t){ x, 0, z }, 0, (float)i, 0, 1, R3D_SHADOW);
        r3d_draw_flags(&R, kind[i % 3], (v3_t){ x, 0, z }, 0, (float)i, 0, 1, R3D_SMOOTH);
        r3d_draw_flags(&R, &fence, (v3_t){ x + 0.45f, 0.34f, z + 0.5f }, 0, 0.6f, 0, 0.3f, 0);
        r3d_draw_flags(&R, &glass, (v3_t){ x - 0.45f, 0.5f, z - 0.3f }, 0, -0.4f, 0, 0.25f, 0);
        r3d_draw_flags(&R, &cube, (v3_t){ x + 0.4f, 0.12f, z - 0.4f }, 0, (float)i, 0, 0.12f, 0);
    }
    pose((float)f * 0.05f);             /* in the lower right corner, as a weapon */
    r3d_draw_flags(&R, &hero_tex, (v3_t){ 0.6f, 0.9f, -4.3f }, 0, 3.0f, 0, 0.4f, R3D_SMOOTH | R3D_FRONT);
    scene_light();
    rect2d(8, H - 30, 180, 20, g16_rgb(20, 30, 40));
    text2d(12, H - 28, "HUD 250/250", 0xFFFF);
    for (int k = 0; k < 8; k++)
        spr2d(&sheet[1], k * 16, 0, 16, 16, W - 140 + 16 * k, H - 28);
}

/* ------------------------------------------------- herotris (opt-in)
 * Stage 0 of docs/design/triangle-throughput.md: a hero and a map, the load
 * of a game on the Pi Zero W. `n` is the number of sets. A set is a hero of
 * 1536 triangles (16 bones, a 64 x 32 texture, lit) with its shadow, and a
 * map of 100 pieces of 50 triangles (baked light, two 64 x 32 sheets): 6500
 * triangles a set, 8036 with the shadow. The sets share the map's place
 * (each 3 mm lower) and the camera sees them all. Over the 3D: two parallax
 * bands (the top one opaque, the bottom one with holes), a HUD of 20 sprites
 * and 3 lines of text. After the 3D: 4M instructions of logic (`work`), as
 * the test `queue`. tris60 of the report: the most triangles a frame of 60
 * fps holds. */

static g16_sheet_t hsheet[3];           /* the hero, map A, map B: 64 x 32 */
static r3d_mesh_t hero_h, map_a, map_b;

/* the uv of a mesh made for 128 x 128 texels, in 64 x 32 texels */
static void uv_small(r3d_mesh_t *m)
{
    for (int i = 0; i < m->nfaces * 6; i += 2) {
        m->uv[i] *= 0.5f;
        m->uv[i + 1] *= 0.25f;
    }
}

static void herotris_setup(void)
{
    make_sheet_wh(&hsheet[0], 64, 32, 0, 0);
    make_sheet_wh(&hsheet[1], 64, 32, 1, 0);
    make_sheet_wh(&hsheet[2], 64, 32, 2, 0);
    make_hero(&hero_h, &hsheet[0]);
    uv_small(&hero_h);
    make_tile(&map_a, 5, 0x80A060);     /* 50 faces */
    textured(&map_a, &hsheet[1]);
    uv_small(&map_a);
    bake(&map_a, 120);
    make_tile(&map_b, 5, 0x80A060);
    textured(&map_b, &hsheet[2]);
    uv_small(&map_b);
    bake(&map_b, 100);
}

static void herotris_free(void)
{
    r3d_mesh_free(&hero_h);
    r3d_mesh_free(&map_a);
    r3d_mesh_free(&map_b);
    for (int i = 0; i < 3; i++)
        g16_sheet_free(&hsheet[i]);
}

static void herotris_scene(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0.3f * sinf((float)f * 0.02f), 1.7f, -5, 0.05f, -0.12f, 70);
    shine_light();
    R.shadow_style = 1;
    for (int s = 0; s < n; s++) {
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 10; j++)
                r3d_draw_flags(&R, (i + j) & 1 ? &map_b : &map_a,
                               (v3_t){ -6.3f + 1.4f * (float)i, -0.003f * (float)s, -1.5f + 1.4f * (float)j }, 0, 0, 0,
                               1.4f, 0);
        const float x = -4.2f + 1.2f * (float)(s % 8) + 0.6f * (float)((s / 8) % 2), z = 1.0f + 1.5f * (float)(s / 8);
        pose((float)f * 0.05f + (float)s);
        r3d_draw_flags(&R, &hero_h, (v3_t){ x, 0, z }, 0, (float)s, 0, 1, R3D_SHADOW);
        r3d_draw_flags(&R, &hero_h, (v3_t){ x, 0, z }, 0, (float)s, 0, 1, R3D_SMOOTH);
    }
    scene_light();
}

/* the 2D of the frame, after the 3D has ended (the runner calls it after the job: with the queue the
 * ARM's logic runs while the GPU draws, then the 2D, as a game's d2 records play after the job) */
static void herotris_2d(int n, int f)
{
    (void)n;
    /* the parallax bands of 16 x 16 pieces, 40 across: 4 rows at the top, 3 at the bottom */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 40; c++)
            spr2d(&sheet[0], ((c + f / 2) % 8) * 16, r * 16, 16, 16, c * 16, r * 16);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 40; c++)
            spr2d(&sheet[1], ((c + f) % 8) * 16, (r + 2) * 16, 16, 16, c * 16, H - 48 + r * 16);
    rect2d(8, H - 84, 180, 20, g16_rgb(20, 30, 40));
    text2d(12, H - 82, "HUD 250/250", 0xFFFF);
    text2d(W - 100, 70, "SCORE 12345", 0xFFE0);
    text2d(W / 2 - 40, 70, "ROUND 3", 0xFFFF);
    for (int k = 0; k < 20; k++)
        spr2d(&sheet[1], (k % 8) * 16, 0, 16, 16, 8 + 18 * k, H - 112);
}

/* One hero alone, lit, drawn once on the profile's path: gl = the triangles the GPU's vertex shader placed
 * (a whole mesh, 1536, when the mesh path took it; 0 when it fell back to the triangles one by one),
 * meshes = the meshes it placed. The ramp calls it before the first step. */
static void herotris_probe(int *gl, int *meshes)
{
    gpu3d_stats_t st;
    cls3d();
    r3d_camera(&R, 0, 1.7f, -5, 0.05f, -0.12f, 70);
    shine_light();
    pose(0);
    gpu3d_flush(g, 0);
    gpu3d_take_stats(&st);
    r3d_draw_flags(&R, &hero_h, (v3_t){ 0, 0, 1 }, 0, 0, 0, 1, R3D_SMOOTH);
    gpu3d_flush(g, 0);
    gpu3d_take_stats(&st);
    *gl = (int)st.gltris;
    *meshes = (int)st.glmeshes;
    scene_light();
}

/* ------------------------------------------------- stress (opt-in)
 * The load of herotris in finer steps, for the stress test (stress_phase): n is a number of quarter sets. A
 * quarter set is 25 map pieces; a hero with its shadow is added for every 4 quarters (or part of them). So 4
 * quarters are one herotris set (8036 triangles), 8 are two. The 2D and the logic are the ones of herotris. */

static void stress_scene(int n, int f)
{
    const int pieces = 25 * n, heroes = (n + 3) / 4;
    cls3d();
    r3d_camera(&R, 0.3f * sinf((float)f * 0.02f), 1.7f, -5, 0.05f, -0.12f, 70);
    shine_light();
    R.shadow_style = 1;
    for (int s = 0; s < heroes; s++) {
        for (int p = 100 * s; p < pieces && p < 100 * s + 100; p++) {
            const int i = p % 100 / 10, j = p % 10;
            r3d_draw_flags(&R, (i + j) & 1 ? &map_b : &map_a,
                           (v3_t){ -6.3f + 1.4f * (float)i, -0.003f * (float)s, -1.5f + 1.4f * (float)j }, 0, 0, 0,
                           1.4f, 0);
        }
        const float x = -4.2f + 1.2f * (float)(s % 8) + 0.6f * (float)((s / 8) % 2), z = 1.0f + 1.5f * (float)(s / 8);
        pose((float)f * 0.05f + (float)s);
        r3d_draw_flags(&R, &hero_h, (v3_t){ x, 0, z }, 0, (float)s, 0, 1, R3D_SHADOW);
        r3d_draw_flags(&R, &hero_h, (v3_t){ x, 0, z }, 0, (float)s, 0, 1, R3D_SMOOTH);
    }
    scene_light();
}

/* ------------------------------------------------- cel (opt-in)
 * Q: what the cel shader costs on top of the lit hero. The scene is `heroes` with the textured hero
 * (1536 triangles, 16 bones, lit by the sun): n heroes in rows of 8. Five tests, the same scene:
 * lit or cel (4 bands), with the ARM raster's dither on or off (r3d_dither). The vertex programs
 * of GPU+VS cut the light (bm3d 7.5); the ARM and the Mali keep the ARM path. `cel_outline` is the lit
 * hero with the outline: flag R3D_OUTLINE, colour 0x2030A0, width 0.03, the mesh again as an inverted hull.
 * The report's C lines give the ms of the same n in the five tests. */

static int cel_bands, cel_dither_off, cel_outline;

static void cel_on_(int bands, int dither_off, int outline)
{
    cel_bands = bands;
    cel_dither_off = dither_off;
    cel_outline = outline;
    r3d_cel(&R, bands, 0.35f);
    r3d_dither(!dither_off);
    R.cel_rgb = outline ? 0x2030A0u : R3D_CEL_NO_OUTLINE;       /* cel3d(0, 0.35, 0x2030A0, 0.03) */
    R.cel_width = 0.03f;
}

static void cel_setup_lit(void)  { cel_on_(0, 0, 0); }
static void cel_setup_cel(void)  { cel_on_(4, 0, 0); }
static void cel_setup_lit_nd(void) { cel_on_(0, 1, 0); }
static void cel_setup_cel_nd(void) { cel_on_(4, 1, 0); }
static void cel_setup_outline(void) { cel_on_(0, 0, 1); }

static void cel_free(void)
{
    r3d_cel(&R, 0, 0.35f);
    r3d_dither(1);
    R.cel_rgb = R3D_CEL_NO_OUTLINE;
    R.cel_width = 0.02f;
    cel_bands = cel_dither_off = cel_outline = 0;
}

static void cel_scene(int n, int f)
{
    cls3d();
    r3d_camera(&R, 0, 2.2f, -4, 0, -0.18f, 60);
    shine_light();
    for (int i = 0; i < n; i++) {
        const float x = -4.2f + 1.2f * (float)(i % 8) + 0.6f * (float)((i / 8) % 2), z = 1.0f + 1.5f * (float)(i / 8);
        pose((float)f * 0.05f + (float)i);
        r3d_draw_flags(&R, &hero_tex, (v3_t){ x, 0, z }, 0, (float)i, 0, 1, R3D_SMOOTH | (cel_bands ? R3D_CEL : 0u) | (cel_outline ? R3D_OUTLINE : 0u));
    }
    scene_light();
}

/* ------------------------------------------------- the 2D scenes (opt-in)
 * Tests to measure 2D work (map, sprites, stacks, fade, light) on the Pi
 * Zero W. They run only when `tests=` names them (`optin`), so the default
 * run, the score and the old reports stay as they were. Every scene is fixed:
 * the positions come from a hash of the item number and the frame number,
 * never from the time. `n` is the load; fill_px counts the screen pixels the
 * frame asks to draw (ns_px_frame in the report).
 *
 * Hooks, not developed here (they wait for their code in bm-core):
 *   stackv     the vertical stacks of PR 90 (a test_t "stackv" after "stack")
 *   particles  the 3D particles of PR 99 (a test_t "particles")
 * TODO: add them as opt-in tests of this block when those PRs are merged. */

static uint32_t hash2d(uint32_t i)
{
    i ^= i >> 16;
    i *= 0x7FEB352Du;
    i ^= i >> 15;
    i *= 0x846CA68Bu;
    i ^= i >> 16;
    return i;
}

#define MAP8_W 96                       /* cells of a layer: 768 x 448 pixels */
#define MAP8_H 56
#define MAP8_LAYERS 8
static g16_map_t map8_layer[MAP8_LAYERS];
static float map8_cover[MAP8_LAYERS];   /* the part of the screen a layer fills */

static void map8_setup(void)
{
    for (int l = 0; l < MAP8_LAYERS; l++) {
        g16_map_t *m = &map8_layer[l];
        m->w = MAP8_W;
        m->h = MAP8_H;
        m->cells = calloc((size_t)MAP8_W * MAP8_H, sizeof *m->cells);
        int used = 0;
        for (int i = 0; m->cells && i < MAP8_W * MAP8_H; i++) {
            const uint32_t h = hash2d((uint32_t)(l * 100003 + i));
            /* layers 0-2: every cell, opaque tiles; 3-7: half the cells, tiles with holes (alpha) */
            if (l < 3 || (h >> 8 & 1)) {
                m->cells[i] = (uint16_t)(1 + h % 255);
                used++;
            }
        }
        map8_cover[l] = (float)used / (float)(MAP8_W * MAP8_H);
    }
}

static void map8_free(void)
{
    for (int l = 0; l < MAP8_LAYERS; l++) {
        free(map8_layer[l].cells);
        map8_layer[l].cells = NULL;
    }
}

/* n layers (8 = the scene) of map() at 640x360, each scrolling faster than the one below */
static void map8_scene(int n, int f)
{
    g16_cls(g, g16_rgb(10, 10, 30));
    for (int l = 0; l < n && l < MAP8_LAYERS; l++) {
        if (!map8_layer[l].cells)
            continue;
        const int sx = (f * (l + 1) * 5) % 120, sy = (f * (l + 1)) % 64;
        g16_map(g, &sheet[l < 3 ? 0 : 1], &map8_layer[l], sx / 8, sy / 8, -(sx % 8), -(sy % 8), W / 8 + 1, H / 8 + 1);
        fill_px += (float)(W * H) * map8_cover[l];
    }
}

/* a sprite of 16 x 16 with flip; the GPU draws it in GPU+2D, else the ARM */
static void spr2d_flip(const g16_sheet_t *s, int sx, int sy, int x, int y, int fx, int fy)
{
    fill_px += 16.0f * 16.0f;
    if (cur_2d && R.backend && gpu3d_blit2d(g, s, sx, sy, 16, 16, x, y, 1, fx, fy))
        return;
    flush2d();
    g16_sspr(g, s, sx, sy, 16, 16, x, y, fx, fy);
}

/* n sprites of 16 x 16: half opaque (sheet 0), half with holes (sheet 1), some flipped */
static void sprites_scene(int n, int f)
{
    cls3d();                            /* the page of the GPU too (GPU+2D) */
    for (int i = 0; i < n; i++) {
        const uint32_t h = hash2d((uint32_t)i);
        spr2d_flip(&sheet[i & 1], (int)(h & 7) * 16, (int)(h >> 3 & 7) * 16,
                   (int)((h >> 6) % (uint32_t)(W - 16) + (uint32_t)(f * (1 + (int)(h >> 29)))) % (W - 16),
                   (int)((h >> 15) % (uint32_t)(H - 16)), (int)(h >> 26 & 1), (int)(h >> 27 & 1));
    }
}

/* n stacks of 8 slices (16 x 16, sheet 1 has holes), turning, in three sizes: a fixed pile */
static void stack_scene(int n, int f)
{
    g16_cls(g, g16_rgb(10, 10, 30));
    for (int i = 0; i < n; i++) {
        const uint32_t h = hash2d((uint32_t)i + 77u);
        const float scale = 1.0f + 0.5f * (float)(i % 3);
        fill_px += 8.0f * 256.0f * scale * scale;
        g16_stack(g, &sheet[1], 0, 0, 16, 16, 8, 40 + (int)(h % (uint32_t)(W - 80)),
                  40 + (int)(h >> 12 & 0xFF) % (H - 80), (float)(h >> 20 & 63) * 0.1f + (float)f * 0.05f, scale, 2.0f);
    }
}

/* the background of fade and light: blocks of the colours the fade has tables for */
static const uint16_t fx_col[4] = { 0xF800, 0x07E0, 0x001F, 0xFFE0 };      /* RGB565 */

static void fx_back(void)
{
    g16_cls(g, g16_rgb(40, 40, 60));
    for (int i = 0; i < 12; i++)
        g16_rectfill(g, (i % 4) * (W / 4), (i / 4) * (H / 3), W / 4, H / 3, fx_col[(i + i / 4) & 3]);
    fill_px += (float)(W * H);          /* the pass over the whole screen */
}

static g16_fade_t fx_fade;
static g16_light_t fx_light;
static int fx_ok;

/* the QPU profile: dark_end through P->fade_qpu (the gpu3d_qpu=1 hook), the ARM when it is not there or
 * the QPUs did not do the frame; the frames of each path are counted for the report */
static int cur_qpu;
static int fade_frames_qpu, fade_frames_arm;
static char fade_line[160];             /* the report's qpufade line; empty when the QPU profile did not run */

static void fade_setup(void)
{
    fade_frames_qpu = fade_frames_arm = 0;
    fx_ok = g16_fade_init(&fx_fade, W, H) == 0;
    if (!fx_ok)
        return;
    g16_fade_reset(&fx_fade, 16);
    for (int c = 0; c < 4; c++) {       /* the table of each block colour: darker to itself */
        uint16_t to[16];
        const uint32_t r = (uint32_t)fx_col[c] >> 11 << 3, gr = (uint32_t)fx_col[c] >> 5 << 2 & 0xFF,
                       b = (uint32_t)fx_col[c] << 3 & 0xFF;
        for (int k = 0; k < 16; k++)
            to[k] = g16_rgb(r * (uint32_t)(k + 1) / 16, gr * (uint32_t)(k + 1) / 16, b * (uint32_t)(k + 1) / 16);
        g16_fade_colour(&fx_fade, fx_col[c], to);
    }
    g16_fade_done(&fx_fade);
}

static void fade_free(void)
{
    if (cur_qpu)
        ksnprintf(fade_line, sizeof fade_line, "key=%d path=%s frames_qpu=%d frames_arm=%d", P->fade_qpu ? 1 : 0,
                  fade_frames_qpu && fade_frames_arm ? "mixed" : fade_frames_qpu ? "qpu" : "arm", fade_frames_qpu,
                  fade_frames_arm);
    if (fx_ok)
        g16_fade_free(&fx_fade);
    fx_ok = 0;
}

/* n lamps (levels, rings dithered) and the fade over the whole screen, on the ARM */
static void fade_scene(int n, int f)
{
    fx_back();
    if (!fx_ok)
        return;
    g16_fade_clear(&fx_fade, 3);
    for (int i = 0; i < n; i++) {
        const uint32_t h = hash2d((uint32_t)i + 5u);
        g16_fade_glow(&fx_fade, (int)(h % (uint32_t)W) + f, (int)(h >> 12 & 0x1FF) % H, 40 + (int)(h >> 24 & 63), 15,
                      128);
    }
    if (cur_qpu && P->fade_qpu) {       /* the hook does the frame by the ARM itself when the QPUs do not */
        if (P->fade_qpu(g, &fx_fade))
            fade_frames_qpu++;
        else
            fade_frames_arm++;
        return;
    }
    g16_fade_apply(g, &fx_fade);
    fade_frames_arm += cur_qpu;
}

static void light_setup(void)
{
    fx_ok = g16_light_init(&fx_light, W, H) == 0;
}

static void light_free(void)
{
    if (fx_ok)
        g16_light_free(&fx_light);
    fx_ok = 0;
}

/* n soft lamps and the light over the whole screen, on the ARM */
static void light_scene(int n, int f)
{
    fx_back();
    if (!fx_ok)
        return;
    g16_light_clear(&fx_light, 0x404040);
    for (int i = 0; i < n; i++) {
        const uint32_t h = hash2d((uint32_t)i + 9u);
        g16_light_add(&fx_light, (float)((int)(h % (uint32_t)W) + f), (float)((int)(h >> 12 & 0x1FF) % H),
                      40.0f + (float)(h >> 24 & 63), 0xFFE0A0u + (h >> 8 & 0x1F) * 0x10100u, 1.0f);
    }
    g16_light_apply(g, &fx_light);
}

/* ---------------------------------------------------------------- tests */

/* a game's logic between two frames: k thousand steps of a random
 * generator (3 instructions each) */
static volatile uint32_t work_sink;
static void work(int k)
{
    uint32_t x = work_sink | 1;
    for (int i = 0; i < k * 1000; i++)
        x = x * 1664525u + 1013904223u;
    work_sink = x;
}

typedef struct {
    const char *id, *name, *unit;       /* the report's key; shown; what n counts */
    int start, max;
    unsigned profiles;
    void (*setup)(void);
    void (*frame)(int n, int f);
    void (*teardown)(void);
    const char *future;                 /* not developed yet: what it waits for */
    int quad_px;                        /* 1: each item is a 320x180 quad (pixels are its point); 1 or 2 (the
                                         * 2D's area): the report gives its cost a pixel (fill_px) */
    int work;                           /* the ARM's work of a frame after its 3D (a game's logic),
                                         * thousands of steps of 3 instructions */
    int optin;                          /* runs only when `tests=` names it (the 2D scenes) */
    void (*frame2d)(int n, int f);      /* the frame's 2D, drawn after its 3D job has ended (NULL: the frame has it) */
    const char *group;                  /* `tests=<group>` runs this test too */
    void (*probe)(int *gl, int *meshes);    /* one hero alone before the ramp: what the vertex shader placed */
    int stress;                         /* after the ramp: the frame at 1x and 2x the load of 60 fps (stress_phase) */
} test_t;

static const test_t tests[] = {
    { "spheres", "spheres, 96 faces, flat", "spheres", 1, 8000, M_LIT | M_AA | M_WC | M_VSS | M_VSA, sp_flat, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_smooth", "spheres, Gouraud", "spheres", 1, 8000, M_LIT, sp_smooth, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_tex", "spheres, textured", "spheres", 1, 8000, M_ARM | M_GPU | M_T16 | M_ETC1 | M_FS2, sp_tex, spheres, sp_undo, NULL, 0,
      0 },
    { "spheres_unlit", "spheres, unlit", "spheres", 1, 8000, M_ALL, sp_unlit, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_baked", "spheres, baked light", "spheres", 1, 8000, M_ALL, sp_baked, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_shine", "spheres, sky, rim, gloss, 4 lamps, fog", "spheres", 1, 8000, M_LIT, sp_shine,
      spheres_shine, sp_undo, NULL, 0, 0 },
    { "heroes", "heroes: 16 bones, 1536 faces", "heroes", 1, 512, M_LIT | M_WC | M_VSS | M_VSA, NULL, heroes, NULL, NULL, 0, 0 },
    { "heroes_tex", "heroes, textured", "heroes", 1, 512, M_LIT | M_T16 | M_ETC1 | M_FS2, NULL, heroes_tex, NULL, NULL, 0, 0 },
    { "heroes_skin", "heroes, textured skins (as the Meshy ones)", "heroes", 1, 512, M_LIT | M_FS2, NULL, heroes_skin, NULL,
      NULL, 0, 0 },
    { "heroes_shadow", "heroes with shadows on a floor", "heroes", 1, 512, M_LIT, NULL, heroes_shadow, NULL, NULL,
      0, 0 },
    { "clip", "map pieces through the near plane", "pieces", 1, 4000, M_ALL, NULL, clip_scene, NULL, NULL, 0, 0 },
    { "tiny", "small faces (about 4 pixels)", "grids", 1, 2000, M_LIT, NULL, tiny, NULL, NULL, 0, 0 },
    { "draws", "draw calls: a cube each", "draws", 1, 40000, M_LIT | M_WC, NULL, draws, NULL, NULL, 0, 0 },
    { "quad_flat", "fill: quads 320x180, flat", "quads", 1, 4000, M_LIT | M_AA, q_flat, quads, q_free, NULL, 1, 0 },
    { "quad_smooth", "fill: quads, Gouraud", "quads", 1, 4000, M_LIT, q_smooth, quads, q_free, NULL, 1, 0 },
    { "quad_tex", "fill: quads, textured", "quads", 1, 4000, M_ARM | M_GPU | M_WC | M_T16 | M_ETC1 | M_FS2, q_tex, quads, q_free, NULL, 1,
      0 },
    { "quad_alpha", "fill: quads, texels with holes", "quads", 1, 4000, M_ARM | M_GPU | M_T16 | M_FS2, q_alpha, quads, q_free,
      NULL, 1, 0 },
    { "quad_screen", "fill: quads, screen-door", "quads", 1, 4000, M_ARM | M_GPU, q_screen, quads, q_free, NULL, 1, 0 },
    { "quad_texscreen", "fill: quads, textured screen-door", "quads", 1, 4000, M_ARM | M_GPU | M_T16 | M_FS2, q_texscreen, quads,
      q_free,
      NULL, 1, 0 },
    { "texswap", "three textures in turn", "quads", 1, 8000, M_ARM | M_GPU, q_tex, texswap, q_free, NULL, 0, 0 },
    { "split", "3D then 2D, again and again", "rounds", 1, 400, M_LIT | M_Q | M_2D | M_VSQ | M_VSQ2, NULL, split, NULL,
      NULL, 0, 0 },
    { "match", "a match: map, heroes, shadows, HUD", "heroes", 1, 256, M_ALL | M_AA | M_Q | M_WC | M_2D | M_T16 | M_ETC1 | M_FS2 | M_VSS | M_VSA, NULL, match, NULL, NULL, 0,
      0 },
    { "mix", "everything at once: map, heroes, skins, shadows, holes, glass, lamps, HUD", "slices", 1, 512,
      M_LIT | M_AA | M_T16 | M_ETC1 | M_FS2, mix_setup, mix, mix_free, NULL, 0, 0 },
    { "queue", "spheres and 4M instructions of logic", "spheres", 1, 8000, M_GPU | M_Q | M_VS | M_VSQ | M_VSQ2, sp_smooth,
      spheres,
      sp_undo,
      NULL, 0, 1300 },
    { "big", "big meshes: models of 10 080 triangles on a map of 14 112", "models", 1, 64, M_ALL | M_VSQ | M_Q | M_VSS | M_VSA,
      big_setup, big, big_free, NULL, 0, 0 },
    { "big_logic", "the same and 4M instructions of logic", "models", 1, 64, M_ALL | M_VSQ | M_Q | M_VSS | M_VSA, big_setup, big,
      big_free, NULL, 0, 1300 },
    { "gpu2d", "sprites and text over the 3D", "sprites", 4, 8000, M_ARM | M_GPU | M_2D, NULL, gpu2d_scene, NULL, NULL,
      2, 0 },
    { "bilinear", "fill: quads, textures filtered (bilinear)", "quads", 1, 4000, M_GPU, q_bilinear, quads,
      q_bilinear_free, NULL, 1, 0 },
    /* the 2D scenes: only with `tests=` (docs/BENCH3D.md), outside the score */
    { "map8", "2D: 8 layers of map(), parallax", "layers", 1, MAP8_LAYERS, M_ARM, map8_setup, map8_scene, map8_free, NULL, 2,
      0, 1 },
    { "sprites", "2D: sprites, flip, alpha", "sprites", 16, 4000, M_ARM | M_2D, NULL, sprites_scene, NULL, NULL, 2, 0, 1 },
    { "stack", "2D: sprite stacks (stack)", "stacks", 1, 600, M_ARM, NULL, stack_scene, NULL, NULL, 2, 0, 1 },
    { "fade", "2D: full-screen fade (dark_end), ARM and QPU", "lamps", 1, 64, M_ARM | M_QPU, fade_setup, fade_scene,
      fade_free, NULL, 2, 0, 1 },
    { "light", "2D: full-screen light, ARM", "lamps", 1, 64, M_ARM, light_setup, light_scene, light_free, NULL, 2, 0, 1 },
    /* herotris (docs/design/triangle-throughput.md): how many triangles a hero and a map hold at 60 fps */
    { "herotris", "hero + map + parallax + HUD + 4M instr of logic", "sets", 1, 64,
      M_ARM | M_GPU | M_VS | M_VSQ | M_VSQ2 | M_VSA | M_VSAQ, herotris_setup, herotris_scene, herotris_free, NULL, 0,
      1300, 1, herotris_2d, NULL, herotris_probe },
    /* stress: the ramp of herotris in quarter sets on the default path, then the frame at 1x and 2x the load of 60 fps */
    { "stress", "stress: herotris at 1x and 2x the load of 60 fps", "quarters", 1, 256, M_VSQ, herotris_setup,
      stress_scene, herotris_free, NULL, 0, 1300, 1, herotris_2d, NULL, NULL, 1 },
    /* cel (Q2): the lit hero against the cel hero, the dither on or off; `tests=cel` runs the four */
    { "cel_lit", "cel: lit hero (reference)", "heroes", 1, 64, M_ARM | M_VS, cel_setup_lit, cel_scene, cel_free, NULL, 0, 0, 1,
      NULL, "cel" },
    { "cel", "cel: 4 bands, dither on", "heroes", 1, 64, M_ARM | M_VS, cel_setup_cel, cel_scene, cel_free, NULL, 0, 0, 1, NULL,
      "cel" },
    { "cel_lit_nd", "cel: lit hero, dither off", "heroes", 1, 64, M_ARM | M_VS, cel_setup_lit_nd, cel_scene, cel_free, NULL, 0,
      0, 1, NULL, "cel" },
    { "cel_nd", "cel: 4 bands, dither off", "heroes", 1, 64, M_ARM | M_VS, cel_setup_cel_nd, cel_scene, cel_free, NULL, 0, 0, 1,
      NULL, "cel" },
    { "cel_outline", "cel: lit hero with the outline (inverted hull)", "heroes", 1, 64, M_ARM | M_VS, cel_setup_outline, cel_scene,
      cel_free, NULL, 0, 0, 1, NULL, "cel" },
};
#define NTESTS ((int)(sizeof tests / sizeof tests[0]))

/* numbers measured on the Pi with the drivers before (docs/M33-PRIMA-
 * DOPO.md, stress test at 640x360): loads at 60 and 30 fps (-1 unknown) */
static const struct { const char *test, *version, *when; float n60, n30; } history[] = {
    { "spheres", "0.1", "2026-09 792787f", 31, -1 },
    { "spheres", "0.2", "2026-10-01 6c2fdaa", 70, -1 },
    { "spheres", "1.0", "2026-10-01 d0c7fe8", 182, 389 },
    { "spheres_smooth", "0.2", "2026-10-01 6c2fdaa", 18, 84 },
    { "spheres_smooth", "1.0", "2026-10-01 d0c7fe8", 170, 364 },
    { "spheres_tex", "0.2", "2026-10-01 6c2fdaa", 0, 51 },
    { "spheres_tex", "1.0", "2026-10-01 d0c7fe8", 156, 333 },
    { "quad_flat", "0.2", "2026-10-01 6c2fdaa", 11, -1 },
    { "quad_flat", "1.0", "2026-10-01 d0c7fe8", 200, 430 },
    { "quad_smooth", "0.2", "2026-10-01 6c2fdaa", 5, -1 },
    { "quad_smooth", "1.0", "2026-10-01 d0c7fe8", 199, 427 },
    { "quad_tex", "0.2", "2026-10-01 6c2fdaa", 2, -1 },
    { "quad_tex", "1.0", "2026-10-01 d0c7fe8", 28, 60 },
};
#define NHIST ((int)(sizeof history / sizeof history[0]))

/* the hardware's limits for a test at 60 fps, in its units: the V3D's
 * fill rate (Raspberry Pi: "1 Gpixel/s, 1.5 Gtexel/s"), its triangle
 * setup as measured on the Pi (test g step 6: 3.0 million a second, every
 * face sent); 0 if none */
static float ceiling(const test_t *t, const char **what)
{
    const float frame = 1.0f / 60.0f;
    if (t->quad_px == 1) {
        const int tex = !strcmp(t->id, "quad_tex") || !strcmp(t->id, "quad_alpha");
        *what = tex ? "V3D 1.5 Gtexel/s" : "V3D 1 Gpixel/s";
        return (tex ? 1.5e9f : 1.0e9f) * frame / (320.0f * 180.0f);
    }
    if (!strncmp(t->id, "spheres", 7)) {
        *what = "V3D 3.0 Mtri/s";
        return 3.0e6f * frame / 96.0f;
    }
    if (!strncmp(t->id, "heroes", 6)) {
        *what = "V3D 3.0 Mtri/s";
        return 3.0e6f * frame / (!strcmp(t->id, "heroes_shadow") ? 2.0f * 1536.0f : 1536.0f);
    }
    return 0;
}

/* ---------------------------------------------------------------- the runner */

typedef struct {
    int n;
    float ms, worst;                    /* a frame, with the copy to the screen */
    float gpu_ms, wait_ms;              /* the V3D's work; the ARM waiting for it */
    float jobs;
    float bin_us, rnd_us;               /* the binner's and the renderer's time of the jobs, a frame */
    float instr, wait_instr, dmiss, cycles;  /* the ARM's counters, a frame */
    float tris_in, tris, verts, pixels, gltris;
    float sync_ms;                      /* the ARM inside gpu3d_sync() waiting for the job, a frame (queue only) */
    float fill;                         /* the pixels the frame asked to fill (fill_px) */
    uint32_t core_mhz, gpu_mhz;         /* measured after the step's frames (0: unknown) */
    float clean_kcyc, inval_kcyc;       /* the ARM's whole-cache upkeep around the jobs, a frame */
    uint32_t pool_kb, ovf_kb, ovf_jobs; /* the binner's memory: most a job used; jobs in the overflow */
} sample_t;

typedef struct {
    int ran, nsamples;
    float n60, n30;                     /* the loads at 60 and 30 fps; -1 below the first step */
    int over;                           /* the ramp ended under the limit (max reached, or time) */
    int last_n;                         /* its last load: with over, n60 or n30 may be more */
    sample_t at;                        /* the last step at 60 fps or better (else the first) */
    float secs;
    float tris60, drawn60;              /* the triangles a frame at 60 fps: given, drawn */
    /* the tests with quad_px: the cost of one more pixel (the slope over
     * the steps, ns a pixel = ms a Mpixel; -1 unknown) of the GPU's job
     * time and of the frame, and the GPU's time with no pixel (the line's
     * intercept: binning, clear, tile stores of 640x360) */
    float ns_gpu, ns_ms, gpu0_ms;
    /* queue 2 only: the same ramp if the frame were shown a frame later (the bench has one page, so it is
     * a model): a frame costs the larger of the ARM's side (ms less the wait in gpu3d_sync) and the GPU's */
    int later;
    float tris60_l, drawn60_l;
    /* the steps n = 1..4 (-1: not run): the median ms and the triangles given, for the same-n comparisons */
    float ms_n[4], tris_n[4];
    /* the vertex shader's mesh path on one hero alone (a test with a probe): the triangles and meshes it placed */
    int probed, probe_gl, probe_meshes, vs_probe;
} result_t;

static result_t res[NTESTS][NPROF];
static struct { int have; float n60, n30; } prev[NTESTS][NPROF];
static char prev_name[32];

static void counts(b3d_count_t *c)
{
    if (P->count)
        P->count(c);
    else
        memset(c, 0, sizeof *c);
}

static void overlay(const test_t *t, int pf, int n, float ms, int i)
{
    char line[96];
    ksnprintf(line, sizeof line, "3D Bench %d/%d  %s  %s (bm3d %s)  n=%d  %d.%d ms", i + 1, NTESTS, t->name,
              prof[pf].name, prof_version(pf), n, (int)ms, (int)(ms * 10) % 10);
    g16_rectfill(g, 0, 0, W, 16, 0);
    g16_text(g, 0, 0, line, 0xFFFF);
}

float b3d_median(float *v, int count)
{
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && v[j - 1] > v[j]; j--) {
            const float t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
    if (count <= 0)
        return 0;
    return count & 1 ? v[count / 2] : 0.5f * (v[count / 2 - 1] + v[count / 2]);
}

float b3d_load_at(const float *n, const float *ms, int count, float limit)
{
    int last = -1;
    for (int i = 0; i < count; i++)
        if (ms[i] <= limit)
            last = i;
    if (last < 0)
        return -1;                      /* even the lightest step is too slow */
    if (last == count - 1)
        return n[last];                 /* never over it: at least the last */
    const int i = last;
    if (ms[i + 1] <= ms[i])
        return n[i];
    return n[i] + (limit - ms[i]) / (ms[i + 1] - ms[i]) * (n[i + 1] - n[i]);
}

/* the least-squares line y = a + b x through count points: b (the cost of
 * one more x), a in *at0; -1 (and *at0 -1) with fewer than two distinct x */
float b3d_slope(const float *x, const float *y, int count, float *at0)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < count; i++) {
        sx += x[i];
        sy += y[i];
        sxx += (double)x[i] * x[i];
        sxy += (double)x[i] * y[i];
    }
    const double d = (double)count * sxx - sx * sx;
    if (count < 2 || d <= 1e-9 * sxx) {
        *at0 = -1;
        return -1;
    }
    const double b = ((double)count * sxy - sx * sy) / d;
    *at0 = (float)((sy - b * sx) / count);
    return (float)b;
}

static char qpu_line[256];              /* the QPU row (bm3d 7.x); empty without the key */

/* the user stopped the run (P->stop): the ramps end, nothing is saved */
static int halted;

static int stopping(void)
{
    if (!halted && P->stop && P->stop())
        halted = 1;
    return halted;
}

/* the profile's switches on the driver; -1: the profile cannot run here (no row). *colour_only: the backend is
 * the Mali's (flat colours) */
static int prof_on(int pf, int *colour_only)
{
    *colour_only = 0;
    if (prof[pf].gpu) {
        if (gpu3d_init() != 0 || !gpu3d_backend())
            return -1;
        if ((prof[pf].aa && !gpu3d_msaa()) || (prof[pf].vs > gpu3d_vshader()) ||
            (prof[pf].queue && !gpu3d_queue_ok()))
            return -1;                  /* not on this GPU: no row */
        R.backend = gpu3d_backend();
        *colour_only = R.backend->colour_only;
        gpu3d_drop();
        gpu3d_set_msaa(prof[pf].aa);
        gpu3d_set_vshader(prof[pf].vs);
        gpu3d_set_wc(prof[pf].wc);
        if (prof[pf].wc && !gpu3d_wc())
            return -1;                  /* the MMU did not change it: no row */
        gpu3d_set_tex16(prof[pf].t16);
        if (prof[pf].t16 && !gpu3d_tex16())
            return -1;                  /* the probe did not learn the layout: no row */
        gpu3d_set_etc1(prof[pf].etc1);
        if (prof[pf].etc1 && !gpu3d_etc1())
            return -1;                  /* the probe did not learn ETC1's layout: no row */
        gpu3d_set_fs2(prof[pf].fs2);
        if (prof[pf].fs2 && !gpu3d_fs2())
            return -1;                  /* the probe saw them differ: no row */
        gpu3d_set_sort(prof[pf].sort);
        gpu3d_set_attr(prof[pf].attr);
        if (prof[pf].attr && !gpu3d_attr())
            return -1;                  /* the probe saw them differ: no row */
    } else {
        R.backend = NULL;
    }
    return 0;
}

static void prof_off(void)
{
    gpu3d_set_msaa(0);
    gpu3d_set_vshader(0);
    gpu3d_set_wc(0);
    gpu3d_set_tex16(0);
    gpu3d_set_etc1(0);
    gpu3d_set_fs2(0);
    gpu3d_set_sort(0);
    gpu3d_set_attr(0);
}

static void ramp(int ti, int pf)
{
    const test_t *t = &tests[ti];
    result_t *r = &res[ti][pf];
    static sample_t s[64];
    int ns = 0, colour_only = 0;        /* colour_only: the backend is the Mali's (flat colours) */
    if (prof_on(pf, &colour_only))
        return;
    cur_2d = prof[pf].two_d;
    cur_qpu = pf == PF_QPU;
    if (t->setup)
        t->setup();
    for (int k = 0; k < 4; k++)
        r->ms_n[k] = r->tris_n[k] = -1;
    if (t->probe && R.backend) {
        t->probe(&r->probe_gl, &r->probe_meshes);
        r->probed = 1;
        r->vs_probe = gpu3d_vshader();
    }
    const int frames = P->quick ? 1 : 6, max = P->quick ? (t->start + 3 > 8 ? t->start + 3 : 8) : t->max;
    const uint32_t t0 = P->us();
    gpu3d_stats_t st;
    gpu3d_take_stats(&st);
    for (int n = t->start; n <= max && ns < 64;) {
        sample_t a;
        memset(&a, 0, sizeof a);
        a.n = n;
        t->frame(n, 0);                 /* warm: textures, corners, caches */
        if (R.backend)
            gpu3d_flush(g, 0);
        if (t->frame2d)
            t->frame2d(n, 0);
        gpu3d_set_queue(prof[pf].queue);
        gpu3d_take_stats(&st);
        fill_px = 0;
        float fms[8];
        for (int f = 1; f <= frames; f++) {
            b3d_count_t c0, c1;
            counts(&c0);
            const uint32_t u0 = P->us();
            t->frame(n, f);
            if (R.backend && prof[pf].queue) {
                gpu3d_submit(g, 0);     /* the GPU draws while the ARM works (bm3d 4.1) */
                work(t->work);
                const uint32_t w0 = P->us();
                gpu3d_sync();
                a.sync_ms += (float)(P->us() - w0) / 1000.0f;
            } else {
                if (R.backend)
                    gpu3d_flush(g, 0);
                work(t->work);
            }
            if (t->frame2d)
                t->frame2d(n, f);       /* the 2D on the page the job has finished */
            counts(&c1);
            const uint32_t u1 = P->us();
            a.tris_in += (float)R.tris_in;
            a.tris += (float)R.tris_drawn;
            a.verts += (float)R.verts;
            a.pixels += (float)R.pixels;
            overlay(t, pf, n, a.ms / (float)(f > 1 ? f - 1 : 1), ti);
            const uint32_t show = P->present();
            const float ms = (float)(u1 - u0 + show) / 1000.0f;
            a.ms += ms;
            fms[f - 1] = ms;
            if (ms > a.worst)
                a.worst = ms;
            a.instr += (float)(c1.instr - c0.instr);
            a.wait_instr += (float)(c1.wait_instr - c0.wait_instr);
            a.dmiss += (float)(c1.dmiss - c0.dmiss);
            a.cycles += (float)(c1.cycles - c0.cycles);
            /* the frame's GPU work is done (flushed or synced): a stop here
             * leaves nothing in flight */
            if (stopping())
                break;
        }
        gpu3d_set_queue(0);
        if (halted)
            break;                      /* a step not finished: not a sample */
        if (colour_only && !R.backend) {
            ns = 0;                     /* the backend handed over to the ARM during the step (r3d to_arm): these are
                                         * the ARM's numbers, no GPU row */
            break;
        }
        gpu3d_take_stats(&st);
        const float k = 1.0f / (float)frames;
        a.ms = b3d_median(fms, frames);     /* the counters below: the mean */
        a.instr *= k; a.wait_instr *= k; a.dmiss *= k; a.cycles *= k;
        a.tris_in *= k; a.tris *= k; a.verts *= k; a.pixels *= k;
        a.fill = fill_px * k;
        a.sync_ms *= k;
        a.gpu_ms = (float)gpu3d_busy_us(&st) * k / 1000.0f;
        a.wait_ms = a.gpu_ms;           /* the ARM waits for every job (with the queue: at most) */
        a.jobs = (float)st.jobs * k;
        a.bin_us = (float)st.bin_us * k;
        a.rnd_us = (float)st.render_us * k;
        a.gltris = (float)st.gltris * k;
        a.clean_kcyc = (float)st.clean_cyc * k / 1000.0f;
        a.inval_kcyc = (float)st.inval_cyc * k / 1000.0f;
        a.pool_kb = (st.pool_max + 1023) / 1024;
        a.ovf_kb = (st.ovf_max + 1023) / 1024;
        a.ovf_jobs = st.ovf_jobs;
        if (P->clocks)
            P->clocks(&a.core_mhz, &a.gpu_mhz);
        s[ns++] = a;
        char line[192];
        ksnprintf(line, sizeof line, "b3d %s %s n=%d %d.%02d ms worst %d.%02d, %d tri, %d vtx, %lu instr, "
                  "GPU %d.%02d ms, %d jobs, core %lu GPU %lu MHz", t->id, prof[pf].name, n, (int)a.ms,
                  (int)(a.ms * 100) % 100, (int)a.worst, (int)(a.worst * 100) % 100, (int)a.tris, (int)a.verts,
                  (unsigned long)a.instr, (int)a.gpu_ms, (int)(a.gpu_ms * 100) % 100, (int)a.jobs,
                  (unsigned long)a.core_mhz, (unsigned long)a.gpu_mhz);
        if (P->log)
            P->log(line);
        if (a.ms > LIMIT_MS || P->us() - t0 > RAMP_US || (R.backend && gpu3d_failed()))
            break;
        n = n < 4 ? n + 1 : n + n / 3;
    }
    if (t->teardown)
        t->teardown();
    prof_off();
    cur_2d = 0;
    r->ran = ns > 0;
    r->nsamples = ns;
    r->secs = (float)(P->us() - t0) / 1e6f;
    if (!ns)
        return;
    r->over = s[ns - 1].ms <= LIMIT_MS;
    r->last_n = s[ns - 1].n;
    float sn[64], sms[64], stri[64], sdrawn[64];
    for (int i = 0; i < ns; i++) {
        sn[i] = (float)s[i].n;
        sms[i] = s[i].ms;
        stri[i] = s[i].tris_in;
        sdrawn[i] = s[i].tris;
    }
    r->n60 = b3d_load_at(sn, sms, ns, MS60);
    r->n30 = b3d_load_at(sn, sms, ns, MS30);
    /* the triangles as the load (they grow with it); under the lightest
     * step, the part of it a frame of 60 fps holds */
    r->ns_gpu = r->ns_ms = r->gpu0_ms = -1;
    if (t->quad_px) {
        float sfill[64], sgpu[64], c0;
        for (int i = 0; i < ns; i++) {
            sfill[i] = s[i].fill;
            sgpu[i] = s[i].gpu_ms;
        }
        r->ns_ms = b3d_slope(sfill, sms, ns, &c0) * 1e6f;
        if (prof[pf].gpu && !prof[pf].queue) {      /* the queue's job time is not trustworthy */
            r->ns_gpu = b3d_slope(sfill, sgpu, ns, &r->gpu0_ms) * 1e6f;
            if (r->ns_gpu < 0)
                r->ns_gpu = r->gpu0_ms = -1;
            else if (r->gpu0_ms < 0)
                r->gpu0_ms = 0;         /* noise around nothing */
        }
        if (r->ns_ms < 0)
            r->ns_ms = -1;
    }
    r->tris60 = b3d_load_at(stri, sms, ns, MS60);
    r->drawn60 = b3d_load_at(sdrawn, sms, ns, MS60);
    if (r->n60 < 0) {
        const float k = s[0].ms > 0 ? MS60 / s[0].ms : 0;
        r->tris60 = s[0].tris_in * k;
        r->drawn60 = s[0].tris * k;
    }
    r->at = s[0];
    for (int i = 0; i < ns; i++)
        if (s[i].ms <= MS60)
            r->at = s[i];
    for (int i = 0; i < ns; i++)
        if (s[i].n >= 1 && s[i].n <= 4) {
            r->ms_n[s[i].n - 1] = s[i].ms;
            r->tris_n[s[i].n - 1] = s[i].tris_in;
        }
    if (prof[pf].queue == 2) {
        float sl[64];
        for (int i = 0; i < ns; i++) {
            const float arm = s[i].ms - s[i].sync_ms;
            sl[i] = arm > s[i].gpu_ms ? arm : s[i].gpu_ms;
        }
        r->later = 1;
        r->tris60_l = b3d_load_at(stri, sl, ns, MS60);
        r->drawn60_l = b3d_load_at(sdrawn, sl, ns, MS60);
        if (b3d_load_at(sn, sl, ns, MS60) < 0) {
            const float k = sl[0] > 0 ? MS60 / sl[0] : 0;
            r->tris60_l = s[0].tris_in * k;
            r->drawn60_l = s[0].tris * k;
        }
    }
}

/* ---------------------------------------------------------------- the stress test */

#define STRESS_US 3000000u              /* each load runs 3 s (6 frames in the quick run of the PC) */

typedef struct {
    int ran, n, frames, drops, failed;  /* failed: the driver gave up (V3D hang or timeout) during the load */
    float secs, tris_in, ms, worst, arm_ms, gpu_ms, jobs;
    uint32_t pool_kb, ovf_kb, ovf_jobs;
} stress_t;

static stress_t stress_r[2];            /* 1x, 2x */
static int stress_pf = -1;              /* the profile of the rows (-1: the test did not run) */
static float stress_n60;                /* the ramp's load at 60 fps, in quarter sets (-1: below the first step) */

/* the frame of test ti on profile pf with n quarter sets, for STRESS_US: the mean ms, the worst, the frames over
 * 16.7 ms (drops), the ARM's ms (less the wait for the GPU), the GPU's ms and the binner's memory */
static void stress_run(int ti, int pf, int n, stress_t *o)
{
    const test_t *t = &tests[ti];
    int colour_only = 0;
    memset(o, 0, sizeof *o);
    o->n = n;
    if (prof_on(pf, &colour_only))
        return;
    cur_2d = prof[pf].two_d;
    cur_qpu = 0;
    if (t->setup)
        t->setup();
    gpu3d_stats_t st;
    t->frame(n, 0);
    if (R.backend)
        gpu3d_flush(g, 0);
    if (t->frame2d)
        t->frame2d(n, 0);
    gpu3d_set_queue(prof[pf].queue);
    gpu3d_take_stats(&st);
    fill_px = 0;
    float sync_ms = 0, sum_ms = 0;
    const uint32_t t0 = P->us();
    for (int f = 1; !stopping(); f++) {
        const uint32_t u0 = P->us();
        t->frame(n, f);
        if (R.backend && prof[pf].queue) {
            gpu3d_submit(g, 0);
            work(t->work);
            const uint32_t w0 = P->us();
            gpu3d_sync();
            sync_ms += (float)(P->us() - w0) / 1000.0f;
        } else {
            if (R.backend)
                gpu3d_flush(g, 0);
            work(t->work);
        }
        if (t->frame2d)
            t->frame2d(n, f);
        const uint32_t u1 = P->us();
        o->tris_in += (float)R.tris_in;
        overlay(t, pf, n, sum_ms / (float)(f > 1 ? f - 1 : 1), ti);
        const float ms = (float)(u1 - u0 + P->present()) / 1000.0f;
        sum_ms += ms;
        if (ms > o->worst)
            o->worst = ms;
        if (ms > MS60)
            o->drops++;
        o->frames = f;
        if (R.backend && gpu3d_failed()) {
            o->failed = 1;
            break;
        }
        if (P->quick ? f >= 6 : P->us() - t0 >= STRESS_US)
            break;
    }
    gpu3d_set_queue(0);
    gpu3d_take_stats(&st);
    const float k = o->frames > 0 ? 1.0f / (float)o->frames : 0;
    o->secs = (float)(P->us() - t0) / 1e6f;
    o->ms = sum_ms * k;
    o->tris_in *= k;
    o->arm_ms = (sum_ms - sync_ms) * k;
    o->gpu_ms = (float)gpu3d_busy_us(&st) * k / 1000.0f;
    o->jobs = (float)st.jobs * k;
    o->pool_kb = (st.pool_max + 1023) / 1024;
    o->ovf_kb = (st.ovf_max + 1023) / 1024;
    o->ovf_jobs = st.ovf_jobs;
    o->ran = o->frames > 0;
    if (t->teardown)
        t->teardown();
    prof_off();
    cur_2d = 0;
}

/* after the ramp of the stress test: 1x = the ramp's load at 60 fps (whole quarter sets, at least 1), then 2x.
 * After a failure of the driver at 1x the 2x load does not run. */
static void stress_phase(int ti, int pf)
{
    const result_t *r = &res[ti][pf];
    if (!r->ran || halted)
        return;
    stress_pf = pf;
    stress_n60 = r->n60;
    const int n1 = r->n60 >= 1.0f ? (int)r->n60 : 1;
    stress_run(ti, pf, n1, &stress_r[0]);
    if (!stress_r[0].failed && !halted)
        stress_run(ti, pf, 2 * n1, &stress_r[1]);
}

/* ---------------------------------------------------------------- the score */

/* The score: every technology's load at 60 fps against the load bm3d 2.1
 * (the GPU profile) gave at its best on the Pi Zero W (reports of
 * 2026-10-05 and 06, 640x360), the geometric mean of the ratios times
 * 1000: 1000 is bm3d 2.1 on the Pi, 2000 a driver twice as fast in every
 * test (or 4x in half and 1x in the rest). A test not here is not in it:
 * the logic ones (queue, big_logic), an option off by default (bilinear),
 * the mix (its triangles at 60 fps are the other number). */
static const struct { const char *test; float n60; } score_ref[] = {
    { "spheres", 208 }, { "spheres_smooth", 163 }, { "spheres_tex", 165 }, { "spheres_unlit", 244 },
    { "spheres_baked", 195 }, { "spheres_shine", 112 }, { "heroes", 6.4f }, { "heroes_tex", 6.9f },
    { "heroes_skin", 6.8f }, { "heroes_shadow", 3.4f }, { "clip", 826 }, { "tiny", 19.7f }, { "draws", 1238 },
    { "quad_flat", 203 }, { "quad_smooth", 199 }, { "quad_tex", 120 }, { "quad_alpha", 114 },
    { "quad_screen", 184 }, { "quad_texscreen", 111 }, { "texswap", 1482 }, { "split", 5.6f },
    { "match", 0.98f }, { "big", 0.29f }, { "gpu2d", 1949 },
};
#define NREF ((int)(sizeof score_ref / sizeof score_ref[0]))

static float ref_of(int ti)
{
    for (int i = 0; i < NREF; i++)
        if (!strcmp(score_ref[i].test, tests[ti].id))
            return score_ref[i].n60;
    return 0;
}

/* the drivers scored: as the games get them in this version (bm3d 6.6:
 * the GPU, its vertex shader, its textured shaders with two threads),
 * bm3d 2.1, bm3d 0.2 */
enum { DRV_GAMES, DRV_GPU, DRV_ARM, NDRV };
static const char *const drv_id[NDRV] = { "games", "GPU", "ARM" };

/* the profile whose row stands for a driver in a test: for the games',
 * GPU+VS+FS2 (it runs wherever the GPU does); without its row (the boot's
 * probe did not see the vertex shader: the games do not get it either)
 * GPU+FS2 where it ran (the textured tests; elsewhere the shaders are the
 * GPU's), else the GPU, and the ARM where the GPU did not run (as a game
 * falls back to it); -1 if none ran */
static int drv_row(int drv, int ti)
{
    static const int chain[NDRV][4] = { { PF_VSF, PF_FS2, PF_GPU, PF_ARM }, { PF_GPU, -1, -1, -1 },
                                        { PF_ARM, -1, -1, -1 } };
    for (int k = 0; k < 4 && chain[drv][k] >= 0; k++)
        if (res[ti][chain[drv][k]].ran)
            return chain[drv][k];
    return -1;
}

/* a test's load at 60 fps for the score: under its first step, the part of
 * that step a frame holds (1 hero in 20 ms: 0.83) */
static float load60(const result_t *r)
{
    if (r->n60 > 0)
        return r->n60;
    return r->at.ms > 0 ? (float)r->at.n * MS60 / r->at.ms : 0;
}

/* the score of a driver; how many tests are in it */
static int score_of(int drv, int *ntests)
{
    float sum = 0;
    int k = 0;
    for (int ti = 0; ti < NTESTS; ti++) {
        const float ref = ref_of(ti);
        const int pf = ref > 0 ? drv_row(drv, ti) : -1;
        const float v = pf >= 0 ? load60(&res[ti][pf]) : 0;
        if (v > 0) {
            sum += logf(v / ref);
            k++;
        }
    }
    *ntests = k;
    return k ? (int)(1000.0f * expf(sum / (float)k) + 0.5f) : 0;
}

static int partial(void)
{
    return P->only_tests || P->only_profiles;
}

static int test_index(const char *id)
{
    for (int ti = 0; ti < NTESTS; ti++)
        if (!strcmp(tests[ti].id, id))
            return ti;
    return -1;
}

/* the triangles a frame at 60 fps of the games' driver: in the mix, and
 * the most of any test (which in *most) */
static float tris_mix(float *drawn)
{
    const int ti = test_index("mix"), pf = ti >= 0 ? drv_row(DRV_GAMES, ti) : -1;
    *drawn = pf >= 0 ? res[ti][pf].drawn60 : 0;
    return pf >= 0 ? res[ti][pf].tris60 : 0;
}

static float tris_most(int *most)
{
    float best = 0;
    *most = -1;
    for (int ti = 0; ti < NTESTS; ti++) {
        const int pf = drv_row(DRV_GAMES, ti);
        if (pf >= 0 && res[ti][pf].tris60 > best) {
            best = res[ti][pf].tris60;
            *most = ti;
        }
    }
    return best;
}

static int prev_score;                  /* the last report's (0: none, or a part of the bench) */
static char opts_start[160];            /* the driver's options as the bench started (gpu3d_options_line) */

/* ---------------------------------------------------------------- the report */

static char *rep;
static size_t rep_len, rep_cap;

/* vsnprintf on kvprintf (the kernel's printf: no precision) */
typedef struct { char *b; size_t n, k; } sbuf_t;

static void sb_putc(char c, void *ctx)
{
    sbuf_t *s = ctx;
    if (s->k + 1 < s->n)
        s->b[s->k++] = c;
}

static int vfmt(char *b, size_t n, const char *fmt, va_list ap)
{
    sbuf_t s = { b, n, 0 };
    kvprintf(sb_putc, &s, fmt, ap);
    if (n)
        b[s.k] = 0;
    return (int)s.k;
}

/* src cut to at most n characters, in b */
static const char *cut(char *b, const char *src, int n)
{
    int i = 0;
    for (; src && src[i] && i < n; i++)
        b[i] = src[i];
    b[i] = 0;
    return b;
}

static void put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void put(const char *fmt, ...)
{
    if (rep_len + 512 > rep_cap) {
        size_t cap = rep_cap ? rep_cap * 2 : 16384;
        char *b = realloc(rep, cap);
        if (!b)
            return;
        rep = b;
        rep_cap = cap;
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vfmt(rep + rep_len, rep_cap - rep_len, fmt, ap);
    va_end(ap);
    if (k > 0)
        rep_len += (size_t)k < rep_cap - rep_len ? (size_t)k : rep_cap - rep_len - 1;
}

/* a float with one decimal ("12.3"), for the report */
static const char *f1(char *b, float v)
{
    if (v < 0) {
        ksnprintf(b, 16, "-1");
        return b;
    }
    ksnprintf(b, 16, "%d.%d", (int)v, (int)(v * 10.0f + 0.5f) % 10);
    return b;
}

/* two decimals ("1.23"), for the costs of a pixel; -1 unknown */
static const char *f2(char *b, float v)
{
    if (v < 0) {
        ksnprintf(b, 16, "-1");
        return b;
    }
    const int c = (int)(v * 100.0f + 0.5f);
    ksnprintf(b, 16, "%d.%02d", c / 100, c % 100);
    return b;
}

static int test_idx(const char *id)
{
    for (int ti = 0; ti < NTESTS; ti++)
        if (!strcmp(tests[ti].id, id))
            return ti;
    return -1;
}

/* the fade test on the QPU profile: which path ran, and its load and time against the ARM */
static void report_qpufade(void)
{
    int ti = 0;
    while (ti < NTESTS && strcmp(tests[ti].id, "fade"))
        ti++;
    if (ti >= NTESTS || !res[ti][PF_QPU].ran)
        return;
    const result_t *q = &res[ti][PF_QPU], *a = &res[ti][PF_ARM];
    char b1[16], b2[16], b3[16], b4[16];
    put("qpufade %s n60_qpu=%s ms_qpu=%s n60_arm=%s ms_arm=%s (n60: lamps at 60 fps; ms: 1 lamp)\n", fade_line,
        f1(b1, q->n60), f1(b2, q->ms_n[0]), a->ran ? f1(b3, a->n60) : "NA", a->ran ? f1(b4, a->ms_n[0]) : "NA");
}

/* Q1, how many hero triangles hold 60 fps: one H line a profile of a test with a probe (herotris).
 * sets60 = the sets (a hero, its shadow, a map of 100 pieces) a frame of 60 fps holds; hero_tris60 the hero
 * mesh's triangles in them (1536 a set, without the shadow), hero_shadow_tris60 with the shadow; submitted60
 * and drawn60 the triangles given to r3d and the ones left after its clip (tris60 and drawn60 of the R line);
 * mesh: what the vertex shader did with one hero alone (ACCEPTED: 1536 triangles placed by the mesh path;
 * FALLBACK: 0, the triangles went one by one; PARTIAL; NA: no vertex shader in the profile). arm_ms is ms
 * less the wait in gpu3d_sync(). The later_ columns (queue 2) are a model, see BENCH3D.md. */
static void report_hero(void)
{
    char a[16], b[16], c[16], d[16], e[16], f[16];
    int head = 0;
    for (int ti = 0; ti < NTESTS; ti++) {
        if (!tests[ti].probe)
            continue;
        for (int pf = 0; pf < NPROF; pf++) {
            const result_t *r = &res[ti][pf];
            if (!r->ran)
                continue;
            if (!head) {
                put("hero triangles at 60 fps (640x360; a set is a hero of 1536 triangles, its shadow and a map of 5000)\n");
                put("columns H,test,profile,sets60,hero_tris60,hero_shadow_tris60,submitted60,drawn60,ms,gpu_ms,arm_ms,mesh,"
                    "probe_gltris,probe_meshes,vs_probe,later_submitted60,later_drawn60\n");
                head = 1;
            }
            const sample_t *s = &r->at;
            const float per = s->n > 0 && s->tris_in > 0 ? s->tris_in / (float)s->n : 8036.0f;
            const float sets = r->tris60 / per;
            const char *mesh = !prof[pf].vs || !r->probed ? "NA" : r->probe_gl >= 1536 ? "ACCEPTED"
                               : r->probe_gl == 0 ? "FALLBACK" : "PARTIAL";
            put("H,%s,%s,%s,%d,%d,%d,%d,%s,%s,%s,%s,%d,%d,%d,%s,%s\n", tests[ti].id, prof[pf].name, f2(a, sets),
                (int)(sets * 1536.0f + 0.5f), (int)(sets * 3072.0f + 0.5f), (int)(r->tris60 + 0.5f),
                (int)(r->drawn60 + 0.5f), f1(b, s->ms), f1(c, s->gpu_ms), f1(d, s->ms - s->sync_ms), mesh, r->probe_gl,
                r->probe_meshes, r->vs_probe, r->later ? f1(e, r->tris60_l) : "-", r->later ? f1(f, r->drawn60_l) : "-");
        }
    }
}

/* Q2, the cost of the cel shader: one C line a profile and step n (1 to 4): the ms of the same n heroes in
 * the five cel tests (lit, cel with 4 bands; dither on, off; outline) and the ratios against the lit hero. */
static void report_cel(void)
{
    static const char *const id[5] = { "cel_lit", "cel", "cel_lit_nd", "cel_nd", "cel_outline" };
    int ti[5], ok = 1;
    for (int k = 0; k < 5; k++) {
        ti[k] = test_idx(id[k]);
        ok = ok && ti[k] >= 0;
    }
    if (!ok)
        return;
    int head = 0;
    for (int pf = 0; pf < NPROF; pf++) {
        if (!res[ti[0]][pf].ran && !res[ti[1]][pf].ran)
            continue;
        for (int n = 1; n <= 4; n++) {
            float ms[5];
            for (int k = 0; k < 5; k++)
                ms[k] = res[ti[k]][pf].ran ? res[ti[k]][pf].ms_n[n - 1] : -1;
            if (ms[0] < 0 && ms[1] < 0)
                continue;
            if (!head) {
                put("cel cost: the same n heroes (1536 triangles each, lit by the sun), ms a frame; "
                    "cel = 4 bands, floor 0.35; nd = dither off; outline = hull 0.03, colour 0x2030A0, no bands\n");
                put("columns C,profile,n,tris,lit,cel,lit_nd,cel_nd,outline,cel_over_lit,cel_nd_over_lit_nd,outline_over_lit\n");
                head = 1;
            }
            char v[5][16], q1[16], q2[16], q3[16];
            float tr = 0;                       /* the triangles given at this n (the same in the four tests) */
            for (int k = 0; k < 5; k++) {
                f2(v[k], ms[k]);
                if (res[ti[k]][pf].ran && res[ti[k]][pf].tris_n[n - 1] > tr)
                    tr = res[ti[k]][pf].tris_n[n - 1];
            }
            f2(q1, ms[0] > 0 && ms[1] > 0 ? ms[1] / ms[0] : -1);
            f2(q2, ms[2] > 0 && ms[3] > 0 ? ms[3] / ms[2] : -1);
            f2(q3, ms[0] > 0 && ms[4] > 0 ? ms[4] / ms[0] : -1);
            put("C,%s,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s\n", prof[pf].name, n, (int)(tr + 0.5f), v[0], v[1],
                v[2], v[3], v[4], q1, q2, q3);
        }
    }
}

/* The stress test: the ramp's load at 60 fps (n60, quarter sets) and the frame at 1x and 2x of it. One T line a
 * load; ms the mean, worst the slowest frame, drops the frames over 16.7 ms, arm_ms the ARM's work less its wait for
 * the GPU, gpu_ms the V3D's busy time, pool_kb and ovf_kb the binner's memory, v3d_failed 1 if the driver gave up. */
static void report_stress(void)
{
    if (stress_pf < 0)
        return;
    char a[16];
    put("stress path=%s n60=%s (quarter sets: 4 are one herotris set of 8036 triangles) secs=%d each\n",
        prof[stress_pf].name, f1(a, stress_n60), P->quick ? 0 : (int)(STRESS_US / 1000000u));
    put("columns T,profile,load,n,tris_in,frames,secs,ms,worst,drops,arm_ms,gpu_ms,jobs,pool_kb,ovf_kb,ovf_jobs,v3d_failed\n");
    for (int k = 0; k < 2; k++) {
        const stress_t *o = &stress_r[k];
        if (!o->ran) {
            put("T,%s,%dx,not_run\n", prof[stress_pf].name, k + 1);
            continue;
        }
        char b[6][16];
        put("T,%s,%dx,%d,%d,%d,%s,%s,%s,%d,%s,%s,%s,%lu,%lu,%lu,%d\n", prof[stress_pf].name, k + 1, o->n, (int)o->tris_in,
            o->frames, f1(b[0], o->secs), f1(b[1], o->ms), f1(b[2], o->worst), o->drops, f1(b[3], o->arm_ms),
            f1(b[4], o->gpu_ms), f1(b[5], o->jobs), (unsigned long)o->pool_kb, (unsigned long)o->ovf_kb,
            (unsigned long)o->ovf_jobs, o->failed);
    }
}

static void report(void)
{
    rep_len = 0;
    put("bm 3D Bench\n");
    put("kernel %s\n", P->kernel ? P->kernel : "?");
    put("drivers bm3d %s (%s)\n", BM3D_VERSION, BM3D_BLOCK);
    put("date %s\n", P->date && P->date[0] ? P->date : "unknown (no network time)");
    put("machine %s\n", P->machine ? P->machine : "?");
    put("gpu %s\n", gpu3d_status());
    put("probes %s\n", gpu3d_probe_log()[0] ? gpu3d_probe_log() : "none");
    put("size %dx%d (the page and the render; the bench has no other output size: no 720x405 and no 1080p mode)\n", W, H);
    put("vsprobe gpu3d_vshader=%d (0 none, 1 scenery, 2 heroes) queue_ok=%d queue2=%d\n", gpu3d_vshader(), gpu3d_queue_ok(),
        gpu3d_queue2());
    put("options %s (at the start; each profile sets its own)\n", opts_start);
    put("counters %s\n",P->counting ? pmu_name() : "none");
    put("previous %s\n", prev_name[0] ? prev_name : "none");
    if (P->only_tests || P->only_profiles)      /* a part of the bench, from the monitor's line */
        put("only tests %s, profiles %s\n", P->only_tests ? P->only_tests : "all",
            P->only_profiles ? P->only_profiles : "all");
    put("columns R,test,profile,version,n60,n30,over,ms,worst,tris_in,tris,verts,pixels,gltris,jobs,gpu_ms,"
        "instr,wait_instr,dmiss,cycles,secs,tris60,drawn60,core_mhz,gpu_mhz,clean_kcyc,inval_kcyc,pool_kb,ovf_kb,"
        "ovf_jobs,fill_px,ns_px_gpu,ns_px_frame,gpu0_ms,bin_us,rnd_us\n");
    char a[16], b[16], c[16], d[16], e[16], f[16], g1[16], g2[16];
    for (int ti = 0; ti < NTESTS; ti++) {
        const test_t *t = &tests[ti];
        if (t->future) {
            put("F,%s,%s\n", t->id, t->future);
            continue;
        }
        for (int pf = 0; pf < NPROF; pf++) {
            const result_t *r = &res[ti][pf];
            if (!r->ran)
                continue;
            const sample_t *s = &r->at;
            char h1[16], h2[16], h3[16];
            put("R,%s,%s,%s,%s,%s,%d,%s,%s,%d,%d,%d,%d,%d,%s,%s,%lu,%lu,%lu,%lu,%d,%d,%d,%lu,%lu,%s,%s,%lu,%lu,%lu,"
                "%lu,%s,%s,%s,%lu,%lu\n",
                t->id, prof[pf].name,
                prof_version(pf), f1(a, r->n60), f1(b, r->n30), r->over, f1(c, s->ms), f1(d, s->worst),
                (int)s->tris_in, (int)s->tris, (int)s->verts, (int)s->pixels, (int)s->gltris, f1(e, s->jobs),
                f1(f, s->gpu_ms), (unsigned long)s->instr, (unsigned long)s->wait_instr, (unsigned long)s->dmiss,
                (unsigned long)s->cycles, (int)r->secs, (int)(r->tris60 + 0.5f), (int)(r->drawn60 + 0.5f),
                (unsigned long)s->core_mhz, (unsigned long)s->gpu_mhz, f1(g1, s->clean_kcyc), f1(g2, s->inval_kcyc),
                (unsigned long)s->pool_kb, (unsigned long)s->ovf_kb, (unsigned long)s->ovf_jobs,
                (unsigned long)s->fill, f2(h1, r->ns_gpu), f2(h2, r->ns_ms), f2(h3, r->gpu0_ms),
                (unsigned long)(s->bin_us + 0.5f), (unsigned long)(s->rnd_us + 0.5f));
        }
    }
    /* the cost of a pixel (the fills and the 2D): ns a pixel = ms a Mpixel,
     * the slope over the steps, and what filling a screen of 640x360,
     * 1280x720, 1920x1080 costs at it (the fill only, not the frame's
     * fixed part: the bench draws at 640x360) */
    put("pixel cost: ns a pixel = ms a Mpixel (slope over the steps; GPU: the job's time, queue off), "
        "ms to fill 640x360 / 1280x720 / 1920x1080 at that cost\n");
    put("columns P,test,profile,ns_px_gpu,ns_px_frame,gpu0_ms,gpu_360,gpu_720,gpu_1080,frame_360,frame_720,"
        "frame_1080\n");
    for (int ti = 0; ti < NTESTS; ti++) {
        if (!tests[ti].quad_px || tests[ti].future)
            continue;
        for (int pf = 0; pf < NPROF; pf++) {
            const result_t *r = &res[ti][pf];
            if (!r->ran)
                continue;
            static const float scr[3] = { 640.0f * 360.0f, 1280.0f * 720.0f, 1920.0f * 1080.0f };
            char v[9][16];
            for (int k = 0; k < 3; k++) {
                f2(v[3 + k], r->ns_gpu < 0 ? -1 : r->ns_gpu * scr[k] / 1e6f);
                f2(v[6 + k], r->ns_ms < 0 ? -1 : r->ns_ms * scr[k] / 1e6f);
            }
            put("P,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", tests[ti].id, prof[pf].name, f2(v[0], r->ns_gpu),
                f2(v[1], r->ns_ms), f2(v[2], r->gpu0_ms), v[3], v[4], v[5], v[6], v[7], v[8]);
        }
    }
    report_hero();
    report_cel();
    report_stress();
    if (qpu_line[0])
        put("qpu row (gpu3d_qpu=1): %s\n", qpu_line);
    report_qpufade();
    /* the score (its S lines read by the next run) and the triangles at 60 fps */
    int sc[NDRV], nt[NDRV];
    for (int d = 0; d < NDRV; d++)
        sc[d] = score_of(d, &nt[d]);
    put("score %d (bm3d %s as the games get it, %d of %d tests%s), GPU bm3d 2.1 %d, ARM bm3d 0.2 %d; "
        "1000: bm3d 2.1 at its best on the Pi Zero W\n", sc[DRV_GAMES], BM3D_VERSION, nt[DRV_GAMES], NREF,
        partial() ? ", a part of the bench" : "", sc[DRV_GPU], sc[DRV_ARM]);
    put("columns S,driver,version,score,tests,full\n");
    for (int d = 0; d < NDRV; d++)
        put("S,%s,%s,%d,%d,%d\n", drv_id[d], d == DRV_GAMES ? BM3D_VERSION : d == DRV_GPU ? "2.1" : "0.2", sc[d],
            nt[d], !partial() && nt[d] == NREF);
    float drawn;
    int most;
    const float mixed = tris_mix(&drawn), top = tris_most(&most);
    put("triangles at 60 fps %d in the mix (%d drawn), the most %d (%s)\n", (int)(mixed + 0.5f),
        (int)(drawn + 0.5f), (int)(top + 0.5f), most >= 0 ? tests[most].id : "none");
}

/* the loads of the report before (its R lines) */
static void read_prev(void)
{
    char *text = NULL;
    if (!P->load_last || P->load_last(&text, prev_name, sizeof prev_name) != 0 || !text)
        return;
    for (char *l = text; l && *l;) {
        char *nl = strchr(l, '\n');
        if (nl)
            *nl = 0;
        if (!strncmp(l, "S,games,", 8)) {
            /* S,games,version,score,tests,full: a whole bench's only */
            const char *f = strrchr(l, ',');
            const char *sc = strchr(l + 8, ',');
            if (f && sc && f[1] == '1')
                prev_score = atoi(sc + 1);
        }
        if (l[0] == 'R' && l[1] == ',') {
            char *fld[8] = { 0 };
            int k = 0;
            for (char *p = l + 2; k < 8 && p;) {
                fld[k++] = p;
                p = strchr(p, ',');
                if (p)
                    *p++ = 0;
            }
            if (k >= 5)
                for (int ti = 0; ti < NTESTS; ti++)
                    for (int pf = 0; pf < NPROF; pf++)
                        if (!strcmp(fld[0], tests[ti].id) && !strcmp(fld[1], prof[pf].name)) {
                            prev[ti][pf].have = 1;
                            prev[ti][pf].n60 = strtof(fld[3], NULL);
                            prev[ti][pf].n30 = strtof(fld[4], NULL);
                        }
        }
        l = nl ? nl + 1 : NULL;
    }
    free(text);
}

/* ---------------------------------------------------------------- the pages */

/* the pages in the 6x12 font: 106 columns, a line every LH pixels */
#define LH 13
#define CW 6

#define C_TEXT  0xFFFF
#define C_DIM   g16_rgb(130, 140, 155)
#define C_HEAD  g16_rgb(255, 224, 112)
#define C_GOOD  g16_rgb(128, 255, 144)
#define C_BAD   g16_rgb(255, 128, 128)
#define C_HIST  g16_rgb(150, 150, 160)
#define C_HW    g16_rgb(255, 90, 90)

static const uint16_t prof_col[NPROF] = { 0xFC00 /* orange */, 0x2D7F, 0x8C1F, 0x07F0, 0x07E0, 0xFFE0, 0xF81F, 0x7BEF,
                                         0xFD20, 0xAFE5, 0x5D7F, 0xB5B6, 0x4FFF, 0xFFFF, 0xFBEF, 0xA7F9, 0xFE60, 0xF81F };

static void text(int x, int y, uint16_t c, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void text(int x, int y, uint16_t c, const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vfmt(b, sizeof b, fmt, ap);
    va_end(ap);
    g16_text(g, x, y, b, c);
}

static uint16_t dimmer(uint16_t c)
{
    return (uint16_t)((c >> 1) & 0x7BEF);
}

/* a load: "<1", ">n" (the ramp's last load still fitted), or the number */
static const char *num(char *b, float v, int over, int last)
{
    if (v < 0) ksnprintf(b, 16, "<1");
    else if (over && v >= (float)last) ksnprintf(b, 16, ">%d", last);
    else if (v < 10) ksnprintf(b, 16, "%d.%d", (int)v, (int)(v * 10) % 10);
    else ksnprintf(b, 16, "%d", (int)(v + 0.5f));
    return b;
}

#define BX 140                          /* the bars: from x BX, BL pixels long */
#define BL 380

/* one bar: the 30 fps load dim, the 60 fps load over it, a tick at the
 * last report's 60 fps load (was > 0) */
static void bar(int y, const char *label, uint16_t col, float n60, float n30, float scale, int over, int last,
                float was)
{
    text(0, y, C_TEXT, "%s", label);
    g16_rectfill(g, BX, y + 1, BL, 10, g16_rgb(30, 34, 44));
    const int l30 = n30 > 0 ? (int)fminf((float)BL, n30 * scale) : 0;
    const int l60 = n60 > 0 ? (int)fminf((float)BL, n60 * scale) : 0;
    if (l30 > 0)
        g16_rectfill(g, BX, y + 1, l30, 10, dimmer(col));
    if (l60 > 0)
        g16_rectfill(g, BX, y + 1, l60, 10, col);
    if (was > 0)
        g16_rectfill(g, BX + (int)fminf((float)BL - 2, was * scale), y - 1, 2, 14, 0xFFFF);
    char a[16], b[16], c[24] = "";
    if (was > 0)
        ksnprintf(c, sizeof c, " was %s", num(b, was, 0, 0));
    text(BX + BL + 6, y, C_TEXT, "%s/%s%s", num(a, n60, over, last),
         n30 < 0 && n60 >= 0 ? "?" : num(b, n30, over, last), c);
}

static int pages(void) { return 4 + NTESTS; }     /* the score, the summary in two, the drivers, one a test */

/* the profiles' short names, for the summary's columns (a legend under it) */
static const char *const prof_short[NPROF] = { "ARM", "GPU", "AA", "VS1", "VS", "VSQ", "Q", "WC", "2D", "VSQ2", "T16",
                                               "FS2", "VSS", "VSF", "VSA", "ETC1", "VSAQ", "QPU" };
#define SX 90                           /* the summary: the profiles' columns from x SX, SW pixels each */
#define SW 25
#define S_BEST (SX + SW * NPROF + 6)
#define S_ROWS 15                       /* tests on the summary's first page */

static void page_summary(int part)
{
    text(0, 18, C_HEAD, "Summary%s: the loads at 60 fps of each driver; the best against the ARM (bm3d 0.2) and "
                        "the last report", part ? " (2)" : "");
    text(0, 34, C_DIM, "test");
    for (int pf = 0; pf < NPROF; pf++)
        text(SX + SW * pf, 34, prof_col[pf], "%5s", prof_short[pf]);
    text(S_BEST, 34, C_DIM, "best  x ARM x last");
    int y = 48;
    float gsum[NPROF] = { 0 };
    int gn[NPROF] = { 0 };
    char fut[160] = "";
    for (int ti = 0; ti < NTESTS; ti++) {
        const test_t *t = &tests[ti];
        if (t->future) {
            char b[48], m[8];
            ksnprintf(b, sizeof b, "%s%s (%s)", fut[0] ? ", " : "", t->id, cut(m, t->future, 3));
            if (strlen(fut) + strlen(b) < sizeof fut)
                strcat(fut, b);
            continue;
        }
        /* the mean against the ARM: every test */
        const result_t *arm = &res[ti][PF_ARM];
        int best = -1;
        for (int pf = 0; pf < NPROF; pf++) {
            const result_t *r = &res[ti][pf];
            if (!r->ran)
                continue;
            if (best < 0 || r->n60 > res[ti][best].n60)
                best = pf;
            if (pf && arm->ran && arm->n60 > 0 && r->n60 > 0) {
                gsum[pf] += logf(r->n60 / arm->n60);
                gn[pf]++;
            }
        }
        if (part ? ti < S_ROWS : ti >= S_ROWS)
            continue;                   /* a row of the other page */
        char id[24];
        text(0, y, C_TEXT, "%s", cut(id, t->id, 15));
        for (int pf = 0; pf < NPROF; pf++) {
            const result_t *r = &res[ti][pf];
            char b[16];
            if (r->ran)
                text(SX + SW * pf, y, r->n60 >= 1 ? prof_col[pf] : C_BAD, "%5s", num(b, r->n60, r->over, r->last_n));
            else
                text(SX + SW * pf, y, C_DIM, "    -");
        }
        if (best >= 0) {
            const float b = res[ti][best].n60;
            text(S_BEST, y, prof_col[best], "%s", prof_short[best]);
            if (arm->ran && arm->n60 > 0 && b > 0) {
                const float k = b / arm->n60;
                text(S_BEST + 24, y, C_GOOD, "%4d.%dx", (int)k, (int)(k * 10) % 10);
            }
            if (prev[ti][best].have && prev[ti][best].n60 > 0 && b > 0) {
                const float k = b / prev[ti][best].n60;
                text(S_BEST + 72, y, k >= 0.97f ? C_GOOD : C_BAD, "%2d.%02dx", (int)k, (int)(k * 100) % 100);
            }
        }
        y += LH;
    }
    y += 6;
    if (!part) {
        text(0, y, C_DIM, "more on the next page; then the drivers and a page for each test");
        return;
    }
    /* six a line */
    text(0, y, C_HEAD, "mean against ARM:");
    int k = 0;
    for (int pf = 1; pf < NPROF; pf++)
        if (gn[pf]) {
            const float m = expf(gsum[pf] / (float)gn[pf]);
            text(108 + 88 * (k % 6), y + LH * (k / 6), prof_col[pf], "%-4s %d.%dx", prof_short[pf], (int)m,
                 (int)(m * 10) % 10);
            k++;
        }
    y += LH * ((k + 5) / 6 > 1 ? (k + 5) / 6 : 1) + 6;
    text(0, y, C_DIM, "AA VS1 VS VSQ Q WC 2D VSQ2 T16 FS2 VSS VSF VSA ETC1 QPU: GPU+AA, GPU+VS1... (the profiles: the next page)");
    y += LH;
    if (fut[0])
        text(0, y, C_DIM, "later: %s; %s%s", fut, prev_name[0] ? "against " : "the first report", prev_name);
    else
        text(0, y, C_DIM, "%s%s", prev_name[0] ? "against " : "the first report", prev_name);
}

/* "1 234 567" */
static const char *thousands(char *b, float v)
{
    const int n = v > 0 ? (int)(v + 0.5f) : 0;
    if (n >= 1000000)
        ksnprintf(b, 16, "%d %03d %03d", n / 1000000, n / 1000 % 1000, n % 1000);
    else if (n >= 1000)
        ksnprintf(b, 16, "%d %03d", n / 1000, n % 1000);
    else
        ksnprintf(b, 16, "%d", n);
    return b;
}

/* the first page: the score, the triangles at 60 fps, every test's part */
static void page_score(void)
{
    int nt, ng, na;
    const int sc = score_of(DRV_GAMES, &nt), s21 = score_of(DRV_GPU, &ng), s02 = score_of(DRV_ARM, &na);
    text(0, 18, C_HEAD, "Score: each technology's load at 60 fps against bm3d 2.1 at its best on the Pi Zero W (1000)");
    char b[32], c[16];
    ksnprintf(b, sizeof b, "%d", sc);
    const uint16_t col = !prev_score || partial() ? C_TEXT : sc * 100 >= prev_score * 97 ? C_GOOD : C_BAD;
    g16_text_scaled(g, 0, 36, b, col, 4);
    const int x = 24 * (int)strlen(b) + 18;
    int y = 36;
    text(x, y, C_TEXT, "bm3d %s as the games get it: the GPU, its vertex shader, two-thread shaders", BM3D_VERSION);
    y += LH;
    if (partial())
        text(x, y, C_BAD, "a part of the bench (%d of %d tests): not to compare", nt, NREF);
    else if (prev_score)
        text(x, y, col, "the last report %d (%s), %d of %d tests", prev_score, prev_name, nt, NREF);
    else
        text(x, y, C_DIM, "%d of %d tests; no report with a score before", nt, NREF);
    y += LH;
    text(x, y, C_DIM, "GPU, bm3d 2.1: %d   ARM, bm3d 0.2: %d   (the same tests, today's code)", s21, s02);
    y += LH;
    text(x, y, C_DIM, "geometric mean: 2000 is twice as fast in every test");
    /* the triangles a frame at 60 fps */
    y = 98;
    float drawn;
    int most;
    const float mixed = tris_mix(&drawn), top = tris_most(&most);
    text(0, y, C_HEAD, "Triangles a frame at 60 fps (640x360)");
    y += 16;
    g16_text_scaled(g, 0, y, thousands(b, mixed), mixed > 0 ? C_GOOD : C_DIM, 2);
    const int x2 = 12 * (int)strlen(b) + 14;
    text(x2, y, C_TEXT, "everything at once (mix): map, heroes plain, textured and skins, shadows, holes,");
    text(x2, y + LH, C_TEXT, "glass, sky, lamps, fog, first person, HUD; %s of them drawn (the rest back faces)",
         thousands(c, drawn));
    y += 2 * LH + 4;
    if (most >= 0)
        text(0, y, C_DIM, "the most in a test: %s (%s), %s a second", thousands(b, top), tests[most].id,
             thousands(c, top * 60.0f));
    /* each test's part: its load against bm3d 2.1, the profile that is the driver there */
    y = 162;
    text(0, y, C_HEAD, "each test against bm3d 2.1 (the bar's tick: 1x)");
    y += LH + 2;
    int k = 0;
    for (int ti = 0; ti < NTESTS; ti++) {
        const float ref = ref_of(ti);
        if (ref <= 0)
            continue;
        const int cx = (k & 1) * 320, cy = y + (k / 2) * LH;
        k++;
        char id[24];
        text(cx, cy, C_TEXT, "%s", cut(id, tests[ti].id, 14));
        const int pf = drv_row(DRV_GAMES, ti);
        const float v = pf >= 0 ? load60(&res[ti][pf]) / ref : 0;
        g16_rectfill(g, cx + 90, cy + 2, 140, 8, g16_rgb(30, 34, 44));
        if (v > 0)
            g16_rectfill(g, cx + 90, cy + 2, (int)fminf(140, v * 70), 8, prof_col[pf]);
        g16_rectfill(g, cx + 160, cy, 1, 12, 0xFFFF);
        if (pf >= 0)
            text(cx + 236, cy, v >= 0.97f ? C_GOOD : C_BAD, "%2d.%02dx %s", (int)v, (int)(v * 100) % 100,
                 prof_short[pf]);
        else
            text(cx + 236, cy, C_DIM, "  -");
    }
}

static void page_info(const char *saved)
{
    int n;
    const bm3d_version_t *v = bm3d_versions(&n);
    text(0, 18, C_HEAD, "Drivers: bm3d %s (%s); kernel %s", BM3D_VERSION, BM3D_BLOCK, P->kernel ? P->kernel : "?");
    int y = 34;
    /* the last four versions (the page holds no more), the older in a line */
    const int i0 = n > 4 ? n - 4 : 0;
    if (i0) {
        text(0, y, C_DIM, "%s to %s: docs/DRIVERS.md", v[0].version, v[i0 - 1].version);
        y += LH;
    }
    for (int i = i0; i < n; i++, y += LH)
        text(0, y, !strcmp(v[i].version, BM3D_VERSION) ? C_GOOD : C_TEXT, "%s %-6s %-10s %s", v[i].version,
             v[i].block, v[i].date, v[i].what);
    y += 6;
    text(0, y, C_TEXT, "Profiles reproduce them on today's code: ARM %s, GPU %s, GPU+AA %s, GPU+VS1 %s, GPU+VS %s",
         prof_version(PF_ARM), prof_version(PF_GPU), prof_version(PF_AA), prof_version(PF_VS1),
         prof_version(PF_VS));
    y += LH;
    text(0, y, gpu3d_queue_ok() ? C_TEXT : C_DIM, "GPU+Q, GPU+VS+Q: GPU and GPU+VS with the frame in the queue (bm3d 4.1: "
         "the ARM goes on while the GPU draws)%s", gpu3d_queue_ok() ? "" : ", not on this GPU");
    y += LH;
    text(0, y, C_TEXT, "GPU+WC: jobs' memory uncached (4.5); GPU+2D: the 2D over the 3D in its job (4.8)");
    y += LH;
    text(0, y, C_TEXT, "GPU+VS+Q2: two jobs in flight (5.1); GPU+T16: 16-bit textures (5.2); GPU+ETC1: opaque as ETC1 (7.3)");
    y += LH;
    text(0, y, C_TEXT, "GPU+FS2: textured faces' pixel shaders with two threads (5.3); GPU+VS+S: meshes "
                       "nearest first (5.4)");
    y += LH;
    text(0, y, C_TEXT, "GPU+VS+FS2: the vertex shader and the two-thread shaders, the driver as the games get it (6.6)");
    y += LH;
    text(0, y, C_TEXT, "GPU+VS+A8: the same with compact attributes, colour and light bytes packed (7.1)");
    y += LH;
    text(0, y, C_DIM, "0.1 and 1.0 no longer run: their bars are the numbers the Pi gave then (docs/GPU-BENCHMARK-REPORT.md)");
    y += LH + 6;
    char w[110];
    text(0, y, C_DIM, "machine: %s", cut(w, P->machine ? P->machine : "", 96));
    y += LH;
    text(0, y, C_DIM, "date: %s", P->date && P->date[0] ? P->date : "unknown (no network time)");
    y += LH;
    {
        /* the GPU's status: on more lines where it is long (after a comma) */
        const char *st = gpu3d_status();
        for (int line = 0; *st && line < 3; line++) {
            const size_t room = line ? 103 : 105, n = strlen(st);
            size_t at = n;
            if (n > room)
                for (at = room - 1; at > 40 && !(st[at] == ' ' && st[at - 1] == ','); at--)
                    ;
            ksnprintf(w, sizeof w, "%s%.*s", line ? "  " : "", (int)(at < sizeof w - 3 ? at : sizeof w - 3), st);
            text(0, y, C_DIM, "%s", w);
            y += LH;
            st += at < n ? at + 1 : n;
        }
    }
    if (P->counting)
        text(0, y, C_DIM, "ARM counters: instructions, D-cache misses, cycles (%s)", pmu_name());
    else
        text(0, y, C_DIM, "ARM counters: none on this machine");
    y += LH;
    text(0, y, saved && saved[0] != '!' ? C_GOOD : C_BAD, "report: %s", saved ? saved : "not saved");
    y += LH + 6;
    text(0, y, C_DIM, "Bars: the load at 60 fps bright, at 30 fps dim; white tick: the last report; grey: measured");
    y += LH;
    text(0, y, C_DIM, "on the Pi with the drivers before; red line: the hardware's limit (Raspberry Pi: 1 Gpixel/s,");
    y += LH;
    text(0, y, C_DIM, "1.5 Gtexel/s; the V3D's 3.0 Mtriangles/s measured by the GPU test). Every test goes far beyond");
    y += LH;
    text(0, y, C_DIM, "today's drivers (up to a frame of 40 ms), so the next ones have room to show.");
}

static void page_test(int ti)
{
    const test_t *t = &tests[ti];
    text(0, 18, C_HEAD, "%s: %s at 60 / 30 fps", t->name, t->unit);
    if (t->future) {
        text(0, 44, C_DIM, "Not developed yet: %s.", t->future);
        text(0, 44 + LH, C_DIM, "The test is listed so the bench grows with the drivers: it runs once they can do it.");
        return;
    }
    /* the scale: the largest value shown, the hardware's limit included */
    const char *cw = NULL;
    const float ceil_n = ceiling(t, &cw);
    float top = fmaxf(1, ceil_n);
    for (int pf = 0; pf < NPROF; pf++)
        if (res[ti][pf].ran)
            top = fmaxf(top, fmaxf(res[ti][pf].n60, fmaxf(res[ti][pf].n30, prev[ti][pf].n60)));
    for (int h = 0; h < NHIST; h++)
        if (!strcmp(history[h].test, t->id))
            top = fmaxf(top, fmaxf(history[h].n60, history[h].n30));
    const float scale = (float)BL / (top * 1.05f);
    int y = 36;
    const int y0 = y;
    char label[40];
    for (int pf = 0; pf < NPROF; pf++) {
        const result_t *r = &res[ti][pf];
        if (!r->ran)
            continue;
        ksnprintf(label, sizeof label, "%-8s bm3d %s", prof[pf].name, prof_version(pf));
        bar(y, label, prof_col[pf], r->n60, r->n30, scale, r->over, r->last_n,
            prev[ti][pf].have ? prev[ti][pf].n60 : 0);
        y += 16;
    }
    for (int h = 0; h < NHIST; h++)
        if (!strcmp(history[h].test, t->id)) {
            char when[12];
            ksnprintf(label, sizeof label, "Pi bm3d %s %s", history[h].version, cut(when, history[h].when, 7));
            bar(y, label, C_HIST, history[h].n60, history[h].n30, scale, 0, 0, 0);
            y += 16;
        }
    if (ceil_n > 0) {
        const int x = BX + (int)fminf((float)BL - 1, ceil_n * scale);
        g16_rectfill(g, x, y0 - 2, 2, y - y0 + 2, C_HW);
        text(0, y, C_HW, "%s: %d at 60 fps", cw, (int)ceil_n);
        y += 16;
    }
    /* the work behind the 60 fps load (the last step that fitted) */
    y += 6;
    text(0, y, C_HEAD, "at 60 fps   load    ms fps worst  tris  verts  Mpix GPUms jobs  Minstr  wait instr/tri "
                       "instr/n  D$miss");
    y += LH;
    for (int pf = 0; pf < NPROF && y < H - 2 * LH; pf++) {
        const result_t *r = &res[ti][pf];
        if (!r->ran)
            continue;
        const sample_t *s = &r->at;
        const float busy = s->instr - s->wait_instr;
        text(0, y, prof_col[pf], "%-8s %7d %3d.%d %3d %3d.%d %5d %6d %3d.%d %3d.%d %4d %4d.%02d %2d.%02d %9d %8d %7d",
             prof[pf].name, s->n, (int)s->ms, (int)(s->ms * 10) % 10, s->ms > 0 ? (int)(1000.0f / s->ms + 0.5f) : 0,
             (int)s->worst, (int)(s->worst * 10) % 10,
             (int)s->tris, (int)s->verts, (int)(s->pixels / 1e6f), (int)(s->pixels / 1e5f) % 10, (int)s->gpu_ms,
             (int)(s->gpu_ms * 10) % 10, (int)(s->jobs + 0.5f), (int)(busy / 1e6f), (int)(busy / 1e4f) % 100,
             (int)(s->wait_instr / 1e6f), (int)(s->wait_instr / 1e4f) % 100,
             s->tris > 0 ? (int)(busy / s->tris) : 0, s->n > 0 ? (int)(busy / (float)s->n) : 0, (int)s->dmiss);
        y += LH;
    }
    y += 4;
    if (!P->counting)
        text(0, y, C_DIM, "no ARM counters on this machine: the instruction columns are 0");
    else
        text(0, y, C_DIM, "Minstr: the ARM's instructions a frame but those spent waiting for the GPU (wait)");
}

static void draw_page(int i, const char *saved)
{
    const font_t *f = g->font;
    g->font = &font_console_6x12;
    g16_cls(g, g16_rgb(10, 14, 20));
    text(0, 2, C_HEAD, "bm 3D Bench  bm3d %s (%s)", BM3D_VERSION, BM3D_BLOCK);
    text(W - 11 * CW, 2, C_DIM, "page %2d/%d", i + 1, pages());
    if (i == 0)
        page_score();
    else if (i < 3)
        page_summary(i - 1);
    else if (i == 3)
        page_info(saved);
    else
        page_test(i - 4);
    const char *back = P->back ? P->back : "B";
    text(W - (26 + (int)strlen(back)) * CW, H - LH, C_DIM, "left/right: pages, %s: back", back);
    g->font = f;
}

/* ---------------------------------------------------------------- run */

/* name is in the comma list (NULL: everything is) */
static int listed(const char *list, const char *name)
{
    if (!list || !*list)
        return 1;
    const size_t n = strlen(name);
    for (const char *s = list; *s;) {
        const char *e = strchr(s, ',');
        const size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == n && !strncmp(s, name, n))
            return 1;
        if (!e)
            break;
        s = e + 1;
    }
    return 0;
}

/* the test runs the profile: its list, and GPU+VS+FS2 (the driver as the
 * games get it, for the score) wherever the GPU runs but with an option
 * of its own (bilinear) */
static int runs(const test_t *t, int pf)
{
    if (pf == PF_VSF)
        return (t->profiles & (M_GPU | M_VS)) && strcmp(t->id, "bilinear") != 0;
    return t->profiles >> pf & 1;
}

static int prof_listed(int pf)
{
    return listed(P->only_profiles, prof[pf].name) || listed(P->only_profiles, prof_short[pf]);
}

int b3d_run(const b3d_platform_t *plat)
{
    P = plat;
    g = P->g;
    memset(res, 0, sizeof res);
    memset(stress_r, 0, sizeof stress_r);
    stress_pf = -1;
    memset(prev, 0, sizeof prev);
    prev_name[0] = 0;
    prev_score = 0;
    halted = 0;
    gpu3d_options_line(opts_start, sizeof opts_start);
    if (r3d_init(&R, g) != 0)
        return -1;
    meshes_make();
    scene_light();
    read_prev();
    const uint32_t t0 = P->us();
    for (int ti = 0; ti < NTESTS && !stopping(); ti++)
        for (int pf = 0; pf < NPROF && !stopping(); pf++)
            if (!tests[ti].future && (!tests[ti].optin || P->only_tests) && runs(&tests[ti], pf) &&
                (listed(P->only_tests, tests[ti].id) || (tests[ti].group && listed(P->only_tests, tests[ti].group))) &&
                prof_listed(pf)) {
                ramp(ti, pf);
                if (tests[ti].stress)
                    stress_phase(ti, pf);
            }
    R.backend = NULL;
    meshes_free();
    r3d_free(&R);
    qpu_line[0] = 0;
    if (P->qpu_row && !stopping()) {    /* the QPU row: gpu3d_qpu=1 only */
        P->qpu_row(qpu_line, sizeof qpu_line);
        if (P->log && qpu_line[0])
            P->log(qpu_line);
    }
    if (stopping()) {                   /* stopped (or the key came with the last frame): no report */
        if (P->log)
            P->log("b3d stopped by the user: no report");
        free(rep);
        rep = NULL;
        rep_cap = rep_len = 0;
        return B3D_STOPPED;
    }
    report();
    put("total %d s\n", (int)((P->us() - t0) / 1000000u));
    char saved[40] = "!";
    int err = 0;
    if (P->save && rep && P->save(rep, rep_len, saved, sizeof saved) != 0) {
        ksnprintf(saved, sizeof saved, "! could not be saved");
        err = -1;
    }
    if (P->log && rep) {
        /* the report, a line at a time */
        for (char *l = rep; *l;) {
            char *nl = strchr(l, '\n');
            if (nl)
                *nl = 0;
            P->log(l);
            if (!nl)
                break;
            *nl = '\n';
            l = nl + 1;
        }
    }
    int page = 0;
    for (;;) {
        draw_page(page, saved);
        P->present();
        if (P->page_shown)
            P->page_shown(page);
        if (P->quick) {
            if (++page == pages())
                break;
            continue;
        }
        if (P->no_wait) {                       /* from a script: the summary a moment, then on */
            const uint32_t t1 = P->us();
            while (P->us() - t1 < 3000000u && P->key() != B3D_KEY_BACK)
                ;
            break;
        }
        int k;
        while ((k = P->key()) == B3D_KEY_NONE)
            ;
        if (k == B3D_KEY_BACK)
            break;
        page = (page + (k == B3D_KEY_LEFT ? pages() - 1 : 1)) % pages();
    }
    free(rep);
    rep = NULL;
    rep_cap = rep_len = 0;
    return err;
}
