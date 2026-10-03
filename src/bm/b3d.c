/*
 * The 3D Bench (b3d.h, docs/BENCH3D.md).
 *
 * Tests: scenes whose load n grows (spheres, heroes, screens of pixels,
 * draws...). Profiles: the renderers, each reproducing a version of the
 * drivers (src/gpu/version3d.h): the ARM (0.2), the GPU (2.1), with MSAA,
 * with the vertex shader for the scenery (3.0) and for every model (3.4).
 * For each test and profile n grows by about a third until a frame takes
 * more than LIMIT_MS; the loads at 60 and 30 fps are interpolated, and the
 * work of the last step under 60 fps is kept for the report.
 *
 * The sphere and quad scenes are those of the stress test (src/bm/
 * stress.c), at the same 640x360: the numbers the Pi gave with the drivers
 * before (docs/M33-PRIMA-DOPO.md) are their bars of history. The loads go
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

enum { PF_ARM, PF_GPU, PF_AA, PF_VS1, PF_VS, PF_VSQ, NPROF };
#define M_ARM (1u << PF_ARM)
#define M_GPU (1u << PF_GPU)
#define M_AA  (1u << PF_AA)
#define M_VS1 (1u << PF_VS1)
#define M_VS  (1u << PF_VS)
#define M_VSQ (1u << PF_VSQ)
#define M_ALL (M_ARM | M_GPU | M_VS1 | M_VS)
#define M_LIT (M_ARM | M_GPU | M_VS)    /* models lit by the sun: VS1 is the GPU for them */

/* queue (M35): the end of each frame started on the GPU, the ARM's work of
 * the frame (the test's `work`) done meanwhile */
static const struct { const char *name; int gpu, aa, vs, queue; } prof[NPROF] = {
    { "ARM", 0, 0, 0, 0 }, { "GPU", 1, 0, 0, 0 }, { "GPU+AA", 1, 1, 0, 0 }, { "GPU+VS1", 1, 0, 1, 0 },
    { "GPU+VS", 1, 0, 2, 0 }, { "GPU+VS+Q", 1, 0, 2, 1 },
};

static const char *prof_version(int p) { return bm3d_mode_q(prof[p].gpu, prof[p].vs, prof[p].queue); }

/* ---------------------------------------------------------------- meshes */

static r3d_mesh_t sphere, quad, cube, tile, grid, hero, hero_tex, hero_skin;
static g16_sheet_t sheet[3];
static float bones[16][12];
static uint8_t vbone[16 * 64], vbone_skin[16 * 64];
static unsigned flags;                  /* r3d flags of the spheres and quads */

/* 128x128 checkers in two colours, one sheet a variant (k) */
static void make_sheet(g16_sheet_t *s, int k, int holes)
{
    static const uint32_t pairs[3][2] = { { 0x2060D0, 0xF0D040 }, { 0xD04030, 0xF0F0F0 }, { 0x30A050, 0x202020 } };
    g16_sheet_alloc(s, 128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            const uint32_t c = pairs[k][((x >> 3) ^ (y >> 3)) & 1];
            g16_sheet_set(s, x, y, g16_rgb24(c), !(holes && ((x >> 2) & 3) == 0 && ((y >> 2) & 3) == 0));
        }
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 16; cx++)
            g16_sheet_update_cell(s, cx, cy);
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
    }
    scene_light();
}

static void q_flat(void) { flags = 0; make_quad(&quad, 0x80C0FF, NULL); }
static void q_smooth(void) { flags = R3D_SMOOTH; make_quad(&quad, 0x80C0FF, NULL); }
static void q_tex(void) { flags = 0; make_quad(&quad, 0, &sheet[0]); }
static void q_alpha(void) { flags = 0; make_quad(&quad, 0, &sheet[1]); }
static void q_screen(void) { flags = 0; make_quad(&quad, 0x80C0FF | R3D_SCREEN, NULL); }
static void q_free(void) { r3d_mesh_free(&quad); flags = 0; }

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
        if (R.backend)
            gpu3d_flush(g, 1);
        g16_rectfill(g, (i * 37) % (W - 40), (i * 23) % (H - 10), 40, 10, g16_rgb(200, 200, 40));
    }
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
    if (R.backend)
        gpu3d_flush(g, 0);
    g16_rectfill(g, 8, H - 30, 180, 20, g16_rgb(20, 30, 40));
    g16_rectfill(g, W - 120, H - 30, 110, 20, g16_rgb(20, 30, 40));
    g16_text(g, 12, H - 28, "HUD 250/250", 0xFFFF);
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
    int quad_px;                        /* each item is a 320x180 quad: pixels are its point */
    int work;                           /* the ARM's work of a frame after its 3D (a game's logic),
                                         * thousands of steps of 3 instructions */
} test_t;

static const test_t tests[] = {
    { "spheres", "spheres, 96 faces, flat", "spheres", 1, 8000, M_LIT | M_AA, sp_flat, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_smooth", "spheres, Gouraud", "spheres", 1, 8000, M_LIT, sp_smooth, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_tex", "spheres, textured", "spheres", 1, 8000, M_ARM | M_GPU, sp_tex, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_unlit", "spheres, unlit", "spheres", 1, 8000, M_ALL, sp_unlit, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_baked", "spheres, baked light", "spheres", 1, 8000, M_ALL, sp_baked, spheres, sp_undo, NULL, 0, 0 },
    { "spheres_shine", "spheres, sky, rim, gloss, 4 lamps, fog", "spheres", 1, 8000, M_LIT, sp_shine,
      spheres_shine, sp_undo, NULL, 0, 0 },
    { "heroes", "heroes: 16 bones, 1536 faces", "heroes", 1, 512, M_LIT, NULL, heroes, NULL, NULL, 0, 0 },
    { "heroes_tex", "heroes, textured", "heroes", 1, 512, M_LIT, NULL, heroes_tex, NULL, NULL, 0, 0 },
    { "heroes_skin", "heroes, textured skins (as the Meshy ones)", "heroes", 1, 512, M_LIT, NULL, heroes_skin, NULL,
      NULL, 0, 0 },
    { "heroes_shadow", "heroes with shadows on a floor", "heroes", 1, 512, M_LIT, NULL, heroes_shadow, NULL, NULL,
      0, 0 },
    { "clip", "map pieces through the near plane", "pieces", 1, 4000, M_ALL, NULL, clip_scene, NULL, NULL, 0, 0 },
    { "tiny", "small faces (about 4 pixels)", "grids", 1, 2000, M_LIT, NULL, tiny, NULL, NULL, 0, 0 },
    { "draws", "draw calls: a cube each", "draws", 1, 40000, M_LIT, NULL, draws, NULL, NULL, 0, 0 },
    { "quad_flat", "fill: quads 320x180, flat", "quads", 1, 4000, M_LIT | M_AA, q_flat, quads, q_free, NULL, 1, 0 },
    { "quad_smooth", "fill: quads, Gouraud", "quads", 1, 4000, M_LIT, q_smooth, quads, q_free, NULL, 1, 0 },
    { "quad_tex", "fill: quads, textured", "quads", 1, 4000, M_ARM | M_GPU, q_tex, quads, q_free, NULL, 1, 0 },
    { "quad_alpha", "fill: quads, texels with holes", "quads", 1, 4000, M_ARM | M_GPU, q_alpha, quads, q_free, NULL,
      1, 0 },
    { "quad_screen", "fill: quads, screen-door", "quads", 1, 4000, M_ARM | M_GPU, q_screen, quads, q_free, NULL, 1, 0 },
    { "texswap", "three textures in turn", "quads", 1, 8000, M_ARM | M_GPU, q_tex, texswap, q_free, NULL, 0, 0 },
    { "split", "3D then 2D, again and again", "rounds", 1, 400, M_LIT, NULL, split, NULL, NULL, 0, 0 },
    { "match", "a match: map, heroes, shadows, HUD", "heroes", 1, 256, M_ALL | M_AA, NULL, match, NULL, NULL, 0, 0 },
    { "queue", "spheres and 4M instructions of logic", "spheres", 1, 8000, M_VS | M_VSQ, sp_smooth, spheres, sp_undo,
      NULL, 0, 1300 },
    { "gpu2d", "sprites and text on the GPU", "", 0, 0, 0, NULL, NULL, NULL, "M37: 2D on the GPU", 0, 0 },
    { "bilinear", "filtered textures", "", 0, 0, 0, NULL, NULL, NULL, "M37: quality options", 0, 0 },
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
    if (t->quad_px) {
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
    float instr, wait_instr, dmiss, cycles;  /* the ARM's counters, a frame */
    float tris_in, tris, verts, pixels, gltris;
} sample_t;

typedef struct {
    int ran, nsamples;
    float n60, n30;                     /* the loads at 60 and 30 fps; -1 below the first step */
    int over;                           /* the ramp ended under the limit (max reached, or time) */
    int last_n;                         /* its last load: with over, n60 or n30 may be more */
    sample_t at;                        /* the last step at 60 fps or better (else the first) */
    float secs;
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

static float lerp_n(const sample_t *a, const sample_t *b, float limit)
{
    if (b->ms <= a->ms)
        return (float)a->n;
    return (float)a->n + (limit - a->ms) / (b->ms - a->ms) * (float)(b->n - a->n);
}

static void ramp(int ti, int pf)
{
    const test_t *t = &tests[ti];
    result_t *r = &res[ti][pf];
    static sample_t s[64];
    int ns = 0;
    if (prof[pf].gpu) {
        if (gpu3d_init() != 0 || !gpu3d_backend())
            return;
        if ((prof[pf].aa && !gpu3d_msaa()) || (prof[pf].vs > gpu3d_vshader()) ||
            (prof[pf].queue && !gpu3d_queue_ok()))
            return;                     /* not on this GPU: no row */
        R.backend = gpu3d_backend();
        gpu3d_drop();
        gpu3d_set_msaa(prof[pf].aa);
        gpu3d_set_vshader(prof[pf].vs);
    } else {
        R.backend = NULL;
    }
    if (t->setup)
        t->setup();
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
        gpu3d_set_queue(prof[pf].queue);
        gpu3d_take_stats(&st);
        for (int f = 1; f <= frames; f++) {
            b3d_count_t c0, c1;
            counts(&c0);
            const uint32_t u0 = P->us();
            t->frame(n, f);
            if (R.backend && prof[pf].queue) {
                gpu3d_submit(g, 0);     /* the GPU draws while the ARM works (M35) */
                work(t->work);
                gpu3d_sync();
            } else {
                if (R.backend)
                    gpu3d_flush(g, 0);
                work(t->work);
            }
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
            if (ms > a.worst)
                a.worst = ms;
            a.instr += (float)(c1.instr - c0.instr);
            a.wait_instr += (float)(c1.wait_instr - c0.wait_instr);
            a.dmiss += (float)(c1.dmiss - c0.dmiss);
            a.cycles += (float)(c1.cycles - c0.cycles);
        }
        gpu3d_set_queue(0);
        gpu3d_take_stats(&st);
        const float k = 1.0f / (float)frames;
        a.ms *= k; a.instr *= k; a.wait_instr *= k; a.dmiss *= k; a.cycles *= k;
        a.tris_in *= k; a.tris *= k; a.verts *= k; a.pixels *= k;
        a.gpu_ms = (float)(st.bin_us + st.render_us) * k / 1000.0f;
        a.wait_ms = a.gpu_ms;           /* the ARM waits for every job (with the queue: at most) */
        a.jobs = (float)st.jobs * k;
        a.gltris = (float)st.gltris * k;
        s[ns++] = a;
        char line[160];
        ksnprintf(line, sizeof line, "b3d %s %s n=%d %d.%02d ms worst %d.%02d, %d tri, %d vtx, %lu instr, "
                  "GPU %d.%02d ms, %d jobs", t->id, prof[pf].name, n, (int)a.ms, (int)(a.ms * 100) % 100,
                  (int)a.worst, (int)(a.worst * 100) % 100, (int)a.tris, (int)a.verts, (unsigned long)a.instr,
                  (int)a.gpu_ms, (int)(a.gpu_ms * 100) % 100, (int)a.jobs);
        if (P->log)
            P->log(line);
        if (a.ms > LIMIT_MS || P->us() - t0 > RAMP_US || (R.backend && gpu3d_failed()))
            break;
        n = n < 4 ? n + 1 : n + n / 3;
    }
    if (t->teardown)
        t->teardown();
    gpu3d_set_msaa(0);
    gpu3d_set_vshader(0);
    r->ran = ns > 0;
    r->nsamples = ns;
    r->secs = (float)(P->us() - t0) / 1e6f;
    if (!ns)
        return;
    r->over = s[ns - 1].ms <= LIMIT_MS;
    r->last_n = s[ns - 1].n;
    const float lim[2] = { MS60, MS30 };
    float *out[2] = { &r->n60, &r->n30 };
    for (int k = 0; k < 2; k++) {
        if (s[0].ms > lim[k]) {
            *out[k] = -1;               /* even the first step is too slow */
            continue;
        }
        *out[k] = (float)s[ns - 1].n;   /* never reached: at least the last */
        for (int i = 1; i < ns; i++)
            if (s[i].ms > lim[k]) {
                *out[k] = lerp_n(&s[i - 1], &s[i], lim[k]);
                break;
            }
    }
    r->at = s[0];
    for (int i = 0; i < ns; i++)
        if (s[i].ms <= MS60)
            r->at = s[i];
}

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

static void report(void)
{
    rep_len = 0;
    put("bm 3D Bench\n");
    put("kernel %s\n", P->kernel ? P->kernel : "?");
    put("drivers bm3d %s (%s)\n", BM3D_VERSION, BM3D_BLOCK);
    put("date %s\n", P->date && P->date[0] ? P->date : "unknown (no network time)");
    put("machine %s\n", P->machine ? P->machine : "?");
    put("gpu %s\n", gpu3d_status());
    put("counters %s\n", P->counting ? pmu_name() : "none");
    put("previous %s\n", prev_name[0] ? prev_name : "none");
    put("columns R,test,profile,version,n60,n30,over,ms,worst,tris_in,tris,verts,pixels,gltris,jobs,gpu_ms,"
        "instr,wait_instr,dmiss,cycles,secs\n");
    char a[16], b[16], c[16], d[16], e[16], f[16];
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
            put("R,%s,%s,%s,%s,%s,%d,%s,%s,%d,%d,%d,%d,%d,%s,%s,%lu,%lu,%lu,%lu,%d\n", t->id, prof[pf].name,
                prof_version(pf), f1(a, r->n60), f1(b, r->n30), r->over, f1(c, s->ms), f1(d, s->worst),
                (int)s->tris_in, (int)s->tris, (int)s->verts, (int)s->pixels, (int)s->gltris, f1(e, s->jobs),
                f1(f, s->gpu_ms), (unsigned long)s->instr, (unsigned long)s->wait_instr, (unsigned long)s->dmiss,
                (unsigned long)s->cycles, (int)r->secs);
        }
    }
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

static const uint16_t prof_col[NPROF] = { 0xFC00 /* orange */, 0x2D7F, 0x8C1F, 0x07F0, 0x07E0, 0xFFE0 };

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

static int pages(void) { return 2 + NTESTS; }

static void page_summary(void)
{
    text(0, 18, C_HEAD, "Summary: the loads at 60 fps of each driver; the best against the ARM (bm3d 0.2) and the "
                        "last report");
    text(0, 34, C_DIM, "test");
    for (int pf = 0; pf < NPROF; pf++)
        text(100 + 46 * pf, 34, prof_col[pf], "%7s", prof[pf].name);
    text(380, 34, C_DIM, "best      x ARM  x last fits 60");
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
        char id[24];
        text(0, y, C_TEXT, "%s", cut(id, t->id, 16));
        int best = -1;
        for (int pf = 0; pf < NPROF; pf++) {
            const result_t *r = &res[ti][pf];
            char b[16];
            if (r->ran) {
                text(100 + 46 * pf, y, r->n60 >= 1 ? prof_col[pf] : C_BAD, "%7s", num(b, r->n60, r->over, r->last_n));
                if (best < 0 || r->n60 > res[ti][best].n60)
                    best = pf;
            } else {
                text(100 + 46 * pf, y, C_DIM, "      -");
            }
        }
        const result_t *arm = &res[ti][PF_ARM];
        if (best >= 0) {
            const float b = res[ti][best].n60;
            text(380, y, prof_col[best], "%s", prof[best].name);
            if (arm->ran && arm->n60 > 0 && b > 0) {
                const float k = b / arm->n60;
                text(432, y, C_GOOD, "%4d.%dx", (int)k, (int)(k * 10) % 10);
                for (int pf = 1; pf < NPROF; pf++)
                    if (res[ti][pf].ran && res[ti][pf].n60 > 0) {
                        gsum[pf] += logf(res[ti][pf].n60 / arm->n60);
                        gn[pf]++;
                    }
            }
            if (prev[ti][best].have && prev[ti][best].n60 > 0 && b > 0) {
                const float k = b / prev[ti][best].n60;
                text(486, y, k >= 0.97f ? C_GOOD : C_BAD, "%2d.%02dx", (int)k, (int)(k * 100) % 100);
            }
            text(546, y, b >= 1 ? C_GOOD : C_BAD, "%s", b >= 1 ? "yes" : "no");
        }
        y += LH;
    }
    text(0, y, C_HEAD, "mean against the ARM:");
    for (int pf = 1; pf < NPROF; pf++)
        if (gn[pf]) {
            const float k = expf(gsum[pf] / (float)gn[pf]);
            text(138 + 102 * (pf - 1), y, prof_col[pf], "%s %d.%dx", prof[pf].name, (int)k, (int)(k * 10) % 10);
        }
    y += LH;
    if (prev_name[0])
        text(0, y, C_DIM, "later: %s; against %s", fut, prev_name);
    else
        text(0, y, C_DIM, "later: %s; the first report", fut);
}

static void page_info(const char *saved)
{
    int n;
    const bm3d_version_t *v = bm3d_versions(&n);
    text(0, 18, C_HEAD, "Drivers: bm3d %s (%s); kernel %s", BM3D_VERSION, BM3D_BLOCK, P->kernel ? P->kernel : "?");
    int y = 34;
    for (int i = 0; i < n; i++, y += LH)
        text(0, y, !strcmp(v[i].version, BM3D_VERSION) ? C_GOOD : C_TEXT, "%s %-6s %-10s %s", v[i].version,
             v[i].block, v[i].date, v[i].what);
    y += 6;
    text(0, y, C_TEXT, "Profiles reproduce them on today's code: ARM %s, GPU %s, GPU+AA %s, GPU+VS1 %s, GPU+VS %s",
         prof_version(PF_ARM), prof_version(PF_GPU), prof_version(PF_AA), prof_version(PF_VS1),
         prof_version(PF_VS));
    y += LH;
    text(0, y, gpu3d_queue_ok() ? C_TEXT : C_DIM, "GPU+VS+Q: the same with the frame in the queue (M35: the ARM goes "
         "on while the GPU draws)%s", gpu3d_queue_ok() ? "" : ", not on this GPU");
    y += LH;
    text(0, y, C_DIM, "0.1 and 1.0 no longer run: their bars are the numbers the Pi gave then (docs/M33-PRIMA-DOPO.md)");
    y += LH + 6;
    char w[110];
    text(0, y, C_DIM, "machine: %s", cut(w, P->machine ? P->machine : "", 96));
    y += LH;
    text(0, y, C_DIM, "date: %s", P->date && P->date[0] ? P->date : "unknown (no network time)");
    y += LH;
    {
        /* the GPU's status: on two lines where it is long (after a comma) */
        const char *st = gpu3d_status();
        size_t n = strlen(st), at = n;
        if (n > 105)
            for (at = 104; at > 40 && !(st[at] == ' ' && st[at - 1] == ','); at--)
                ;
        ksnprintf(w, sizeof w, "%.*s", (int)(at < sizeof w ? at : sizeof w - 1), st);
        text(0, y, C_DIM, "%s", w);
        y += LH;
        if (at < n) {
            text(0, y, C_DIM, "  %s", cut(w, st + at + 1, 103));
            y += LH;
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
        page_summary();
    else if (i == 1)
        page_info(saved);
    else
        page_test(i - 2);
    const char *back = P->back ? P->back : "B";
    text(W - (26 + (int)strlen(back)) * CW, H - LH, C_DIM, "left/right: pages, %s: back", back);
    g->font = f;
}

/* ---------------------------------------------------------------- run */

int b3d_run(const b3d_platform_t *plat)
{
    P = plat;
    g = P->g;
    memset(res, 0, sizeof res);
    memset(prev, 0, sizeof prev);
    prev_name[0] = 0;
    if (r3d_init(&R, g) != 0)
        return -1;
    meshes_make();
    scene_light();
    read_prev();
    const uint32_t t0 = P->us();
    for (int ti = 0; ti < NTESTS; ti++)
        for (int pf = 0; pf < NPROF; pf++)
            if (!tests[ti].future && (tests[ti].profiles >> pf & 1))
                ramp(ti, pf);
    R.backend = NULL;
    meshes_free();
    r3d_free(&R);
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
