/*
 * The GPU backend of r3d (src/gpu/gpu3d.c) against the software
 * rasterizer, on the V3D emulator of v3d_emu.c (M33).
 *
 *   test_gpu3d RED_A TEX_SWAP [TFORMAT [MS_LOAD_ONE [DIR]]]
 *
 * RED_A and TEX_SWAP are the emulator's hidden byte orders, TFORMAT its
 * layout of T-format textures (0, 1; 2: none), MS_LOAD_ONE whether a load
 * fills one sample of four with MSAA: the backend's probe must find them.
 * With MSAA the scenes are drawn a third time: they may differ from the
 * GPU's without it only on edges (and not at all where MSAA is not used). Every scene is drawn by the software and by the
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
static r3d_mesh_t sphere, quad, floor_m, cube, lit_quad, glass, lit_box, hero;
static float hero_bones[2][12];
static uint8_t hero_vbone[512];

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

/* M36: a "hero": a sphere (bone 0) and a cube above it (bone 1), lit by
 * the sun; glossy, emissive, screen-door and flat faces */
static void make_hero(void)
{
    r3d_mesh_t s, c;
    r3d_mesh_sphere(&s, 8, 12, 0x4080FF, 0xFFC040);
    r3d_mesh_cube(&c, 0xC06040);
    r3d_mesh_alloc(&hero, s.nverts + c.nverts, s.nfaces + c.nfaces);
    for (int i = 0; i < s.nverts; i++) {
        hero.verts[i] = s.verts[i];
        hero_vbone[i] = 0;
    }
    for (int i = 0; i < c.nverts; i++) {
        hero.verts[s.nverts + i] = (v3_t){ c.verts[i].x * 0.5f, c.verts[i].y * 0.5f + 1.3f, c.verts[i].z * 0.5f };
        hero_vbone[s.nverts + i] = 1;
    }
    for (int f = 0; f < s.nfaces; f++) {
        for (int k = 0; k < 3; k++)
            hero.faces[f * 3 + k] = s.faces[f * 3 + k];
        hero.colors[f] = s.colors[f] | (f % 3 == 0 ? R3D_GLOSSY : 0) | (f % 17 == 0 ? R3D_EMISSIVE : 0);
    }
    for (int f = 0; f < c.nfaces; f++) {
        for (int k = 0; k < 3; k++)
            hero.faces[(s.nfaces + f) * 3 + k] = (uint16_t)(s.nverts + c.faces[f * 3 + k]);
        hero.colors[s.nfaces + f] = c.colors[f] | (f < 2 ? R3D_SCREEN : 0) | (f >= 4 && f < 8 ? R3D_FLAT : 0) |
                                    R3D_GLOSSY;
    }
    hero.bones = (const float (*)[12])hero_bones;
    hero.vbone = hero_vbone;
    hero.nbones = 2;
    r3d_mesh_normals(&hero);
    r3d_mesh_free(&s);
    r3d_mesh_free(&c);
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
    /* Overbit (M34): a quad of a "lit" model (light baked at the corners,
     * warm, the same at every corner: the ARM takes one per face) and a
     * screen-door cube */
    r3d_mesh_alloc(&lit_quad, 4, 2);
    memcpy(lit_quad.verts, floor_m.verts, 4 * sizeof *lit_quad.verts);
    memcpy(lit_quad.faces, floor_m.faces, 6 * sizeof *lit_quad.faces);
    r3d_mesh_alloc_uv(&lit_quad);
    memcpy(lit_quad.uv, floor_m.uv, 12 * sizeof *lit_quad.uv);
    lit_quad.colors[0] = lit_quad.colors[1] = R3D_TEXTURED;
    lit_quad.tex = &sheet;
    lit_quad.clight = malloc(2 * 9);
    for (int i = 0; i < 6; i++) {
        lit_quad.clight[i * 3] = 176;
        lit_quad.clight[i * 3 + 1] = 120;
        lit_quad.clight[i * 3 + 2] = 90;
    }
    r3d_mesh_normals(&lit_quad);
    r3d_mesh_cube(&glass, 0x40C0F0 | R3D_SCREEN);
    /* M36: a "lit" box as Overbit's map has them: textured faces and faces
     * of a colour, light baked at every corner, an emissive face, two
     * faces shown only at the details 0 and 1 */
    r3d_mesh_cube(&lit_box, 0x80C060);
    r3d_mesh_alloc_uv(&lit_box);
    lit_box.clight = malloc((size_t)lit_box.nfaces * 9);
    for (int f = 0; f < lit_box.nfaces; f++) {
        if (f < 4) {
            lit_box.colors[f] = R3D_TEXTURED;
            const float uv[6] = { 64, 31.5f, 95.5f, 0, 95.5f, 31.5f };
            memcpy(lit_box.uv + f * 6, uv, sizeof uv);
        } else if (f < 6) {
            lit_box.colors[f] = 0xF06040 | R3D_EMISSIVE;
        } else if (f < 8) {
            lit_box.colors[f] = 0x4060F0 | 1u << 26 | 2u << 24;     /* details 0 and 1 */
        }
        for (int k = 0; k < 9; k++)
            lit_box.clight[f * 9 + k] = (uint8_t)(60 + (f * 37 + k * 23) % 160);
    }
    lit_box.tex = &sheet;
    r3d_mesh_normals(&lit_box);
}

/* ---------------------------------------------------------------- scenes */

typedef void (*scene_fn)(r3d_t *r, g16_t *g, int gpu);

static void flush(r3d_t *r, g16_t *g, int gpu)
{
    (void)r;
    if (gpu)
        CHECK(gpu3d_flush(g, 0) == 0, "flush: %s (%s)", gpu3d_status(), emu_error);
}

/* M36: unlit meshes inside the view: with the vertex shader on, the GPU
 * places them itself (and throws their back faces away) */
static void s_vshader(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0, -6, 0, 0, 60);
    gpu3d_set_vshader(gpu != 0);
    r3d_draw_flags(r, &sphere, (v3_t){ -1.6f, 0.4f, 0 }, 0.3f, 0.5f, 0, 1.2f, R3D_UNLIT);
    r3d_draw_flags(r, &cube, (v3_t){ 1.4f, -0.2f, 0.5f }, 0.4f, 0.7f, 0.2f, 1.1f, R3D_UNLIT);
    r3d_draw_flags(r, &sphere, (v3_t){ 0.2f, -0.6f, 2 }, 0, 1.0f, 0, 1.4f, R3D_UNLIT);  /* partly behind */
    r3d_draw_flags(r, &sphere, (v3_t){ -1.0f, 0.2f, 8 }, 0, 0, 0, 1.0f, 0);         /* lit: the ARM's way */
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
}

/* M36: "lit" boxes with fog and a lamp, at two levels of detail */
static int vs_off;                      /* the GPU without its vertex shader */
static void s_vshader_lit(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0.5f, -5, 0, -0.1f, 60);
    r3d_light(r, -0.3f, 0.8f, 0.4f, 0.4f);
    r3d_fog(r, 0xC0A080, 3, 10);
    r3d_lamp_rgb(r, 0, 1.2f, 0.3f, 0.5f, 2.5f, 0.8f, 0xFF8040);
    gpu3d_set_vshader(gpu && !vs_off);
    r3d_draw_flags(r, &lit_box, (v3_t){ -1.4f, 0, 1 }, 0.4f, 0.7f, 0, 1.0f, 0);
    r3d_draw_flags(r, &lit_box, (v3_t){ 1.3f, 0.2f, 3 }, 0.2f, -0.6f, 0.1f, 1.2f, R3D_DETAIL(1));
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
    r3d_lamp(r, 0, 0, 0, 0, 0, 0);
    r3d_fog(r, 0, 0, 0);
}

/* M36: meshes the GPU clips: a "lit" floor under the camera and a wall
 * beside it, both reaching behind it (the near plane, the guard band),
 * and a lamp. The fog starts past them: the GPU blends the corners' fog
 * where it cuts a face, the ARM works it out there (as Overbit's map
 * pieces, nearer than its fog). Faces of one light (the ARM's own
 * rasterizer takes a face's light from its first corner). */
static void s_vshader_clip(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0.5f, -5, 0.15f, -0.1f, 70);
    r3d_light(r, -0.3f, 0.8f, 0.4f, 0.4f);
    r3d_fog(r, 0xC0A080, 60, 120);
    r3d_lamp_rgb(r, 0, 0.3f, 0, -3, 2.5f, 0.8f, 0xFF8040);
    gpu3d_set_vshader(gpu && !vs_off);
    r3d_draw_flags(r, &lit_quad, (v3_t){ 0, -1, 2 }, 1.5707963f, 0, 0, 30, 0);
    r3d_draw_flags(r, &lit_quad, (v3_t){ 0.6f, 0, -4 }, 0, 1.5707963f, 0, 3, 0);
    r3d_draw_flags(r, &lit_quad, (v3_t){ 0.6f, 0, -4 }, 0, -1.5707963f, 0, 3, 0);
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
    r3d_lamp(r, 0, 0, 0, 0, 0, 0);
    r3d_fog(r, 0, 0, 0);
}

/* M36: models lit by the sun with two bones (the heroes of Overbit), by
 * the vertex shader at level 2: Gouraud and flat, sky and ground, rim and
 * highlights, a lamp on the smooth ones, the fog; one through the near
 * plane */
static void s_vshader_heroes(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0.6f, -5, 0, -0.05f, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.35f);
    r3d_sky(r, 0xFFF0D0, 0x90B0FF, 0x806040);
    r3d_shine(r, 0.6f, 16, 0.5f);
    r3d_fog(r, 0xC0A080, 4, 14);
    r3d_lamp_rgb(r, 0, -1.6f, 0.5f, -1.2f, 2.0f, 0.9f, 0xFF8040);
    gpu3d_set_vshader(gpu && !vs_off ? 2 : 0);
    r->shadow_style = 1;
    r3d_draw_flags(r, &cube, (v3_t){ 0, -2.2f, 1 }, 0, 0, 0, 1.0f, 0);       /* the floor: a cube's top */
    r3d_draw_flags(r, &cube, (v3_t){ -2.4f, -2.2f, 1 }, 0, 0, 0, 1.0f, 0);
    r3d_draw_flags(r, &cube, (v3_t){ 2.4f, -2.2f, 1.5f }, 0, 0, 0, 1.0f, 0);
    for (int i = 0; i < 4; i++) {
        const float a = 0.5f * (float)i;
        const float b0[12] = { cosf(a), 0, sinf(a), 0, 0, 1, 0, 0, -sinf(a), 0, cosf(a), 0 };
        const float b1[12] = { 1, 0, 0, 0.2f * (float)i, 0, cosf(a), -sinf(a), 0, 0, sinf(a), cosf(a), 0 };
        memcpy(hero_bones[0], b0, sizeof b0);
        memcpy(hero_bones[1], b1, sizeof b1);
        const v3_t at[4] = { { -1.8f, -0.4f, 1 }, { 0, -0.3f, 0 }, { 1.8f, -0.4f, 1.5f }, { 0.3f, 0.2f, -4.6f } };
        if (i < 3)                      /* its shadow on the floor under it */
            r3d_draw_flags(r, &hero, (v3_t){ at[i].x, -1.2f, at[i].z }, 0, 0.3f * (float)i, 0, 0.7f, R3D_SHADOW);
        r3d_draw_flags(r, &hero, at[i], 0, 0.3f * (float)i, 0, 0.7f, i == 2 ? 0 : R3D_SMOOTH);
    }
    /* a first-person model: in front of everything drawn before */
    r3d_draw_flags(r, &hero, (v3_t){ 0.9f, -0.2f, -3.2f }, 0.3f, 0.5f, 0, 0.3f, R3D_FRONT | R3D_SMOOTH);
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
    r3d_lamp(r, 0, 0, 0, 0, 0, 0);
    r3d_fog(r, 0, 0, 0);
    r3d_shine(r, 0.6f, 16, 0);              /* as r3d_init */
    r3d_sky(r, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
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

/* 3D, 2D over it, then 3D partly behind the first: the first part's depth
 * must hide the second. Two frames: the first teaches the backend that
 * this cartridge needs its depth kept between jobs. */
static void s_split(r3d_t *r, g16_t *g, int gpu)
{
    for (int frame = 0; frame < 2; frame++) {
        g16_cls(g, g16_rgb(30, 20, 50));
        g16_rectfill(g, 0, 0, g->w, 12, g16_rgb(200, 200, 0));
        r3d_zclear(r);
        r3d_camera(r, 0, 0, -6, 0, 0, 60);
        r3d_light(r, -0.4f, 0.7f, -0.6f, 0.3f);
        r3d_draw_flags(r, &sphere, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1.5f, R3D_SMOOTH);
        if (gpu)
            CHECK(gpu3d_flush(g, 1) == 0, "split: %s (%s)", gpu3d_status(), emu_error);
        g16_rectfill(g, 60, 220, 200, 40, g16_rgb(20, 200, 90));
        r3d_draw_flags(r, &cube, (v3_t){ 0.9f, 0.2f, 1.5f }, 0.3f, 0.5f, 0, 1.3f, 0);
        r3d_draw_flags(r, &floor_m, (v3_t){ -1.2f, -0.6f, 1.0f }, 0, 0.4f, 0, 0.8f, 0);
        flush(r, g, gpu);
    }
}

/* a page of one colour (cls): the GPU clears the tiles to it instead of
 * loading the page */
static void s_cleared(r3d_t *r, g16_t *g, int gpu)
{
    const uint16_t bg = g16_rgb(20, 90, 60);
    g16_cls(g, bg);
    if (gpu)
        gpu3d_page(1, bg);
    s_spheres(r, g, gpu);
}

/* Overbit (M34): a lit textured quad in the fog, a screen-door cube, the
 * dithered shadow of a sphere on a floor */
static void s_overbit(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 1.5f, -5, 0, -0.25f, 60);
    r3d_light(r, -0.3f, 0.8f, 0.4f, 0.4f);
    r->shadow_style = 1;
    r3d_draw_flags(r, &cube, (v3_t){ 0, -2.2f, 1 }, 0, 0, 0, 1.2f, 0);       /* the floor: a cube's top */
    r3d_draw_flags(r, &sphere, (v3_t){ 0.6f, -1.0f, 1 }, 0, 0, 0, 0.8f, R3D_SHADOW);   /* on the top */
    r3d_draw_flags(r, &sphere, (v3_t){ 0.6f, -0.2f, 1 }, 0, 0, 0, 0.8f, R3D_SMOOTH);
    r3d_fog(r, 0xC0A080, 3, 12);
    r3d_draw_flags(r, &lit_quad, (v3_t){ -1.6f, 0.4f, 2 }, 0, 0.3f, 0, 1, 0);
    r3d_fog(r, 0, 0, 0);
    r3d_draw_flags(r, &glass, (v3_t){ 1.6f, 0.8f, 0 }, 0.3f, 0.5f, 0, 0.6f, 0);
    flush(r, g, gpu);
}

/* the 3D effects, tested against the depth of a cube: points, lines, a
 * sprite with transparent texels */
static void s_effects(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0, -5, 0, 0, 60);
    r3d_light(r, 0, 0, -1, 0.6f);
    r3d_draw_flags(r, &cube, (v3_t){ 0, 0, 0 }, 0.2f, 0.4f, 0, 0.8f, 0);
    for (int i = 0; i < 6; i++)
        r3d_point(r, (v3_t){ -2.0f + 0.8f * (float)i, 0.2f * (float)(i % 2), 0.5f - 0.5f * (float)i }, 0.12f,
                  0xF0E040, i == 5 ? R3D_FX_SCREEN : 0);
    r3d_line(r, (v3_t){ -2, -1, -1 }, (v3_t){ 2, 1.2f, 2 }, 0xFF6020, 3, 0);
    r3d_line(r, (v3_t){ -2, 1, 2 }, (v3_t){ 2, -1.2f, -1 }, 0x60FF20, 2, 0);
    r3d_sprite(r, &sheet, 0, 32, 32, 32, (v3_t){ 1.3f, -0.9f, -0.5f }, 0.9f, 0);
    flush(r, g, gpu);
}

/* msaa: drawn a third time with MSAA (not where the depth goes from a job
 * to the next: MSAA does not keep it) */
static const struct { const char *name; scene_fn fn; int w, h; float limit; int msaa; } scenes[] = {
    { "spheres", s_spheres, 640, 360, 0.02f, 1 },
    { "textures", s_textures, 640, 360, 0.04f, 1 },
    { "noz_zclear", s_noz_zclear, 480, 270, 0.02f, 1 },
    { "many", s_many, 640, 360, 0.05f, 1 },
    { "3 sheets", s_sheets, 640, 360, 0.02f, 1 },
    { "full job", s_full, 640, 360, 0.02f, 0 },
    { "3D 2D 3D", s_split, 640, 360, 0.02f, 0 },
    { "cleared", s_cleared, 320, 180, 0.02f, 1 },
    { "overbit", s_overbit, 640, 360, 0.03f, 1 },
    { "effects", s_effects, 640, 360, 0.02f, 1 },
    { "vshader", s_vshader, 640, 360, 0.02f, 1 },
    { "vshader lit", s_vshader_lit, 640, 360, 0.04f, 1 },
    { "vshader clip", s_vshader_clip, 640, 360, 0.04f, 1 },
    { "vshader heroes", s_vshader_heroes, 640, 360, 0.04f, 1 },
};
#define CLEARED 7                   /* its index: no bar, no load */

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
    uint16_t *a = test_aligned_alloc(16, (size_t)w * h * 2), *b = test_aligned_alloc(16, (size_t)w * h * 2),
             *c = test_aligned_alloc(16, (size_t)w * h * 2);
    uint16_t *pages[3] = { a, b, c };
    r3d_t r;
    g16_t g;
    for (int pass = 0; pass < (scenes[s].msaa ? 3 : 2); pass++) {
        g16_target(&g, pages[pass], (uint32_t)w, w, h, &font);
        g16_cls(&g, g16_rgb(30, 20, 50));
        g16_rectfill(&g, 0, 0, w, 12, g16_rgb(200, 200, 0));      /* drawn by the ARM first */
        r3d_init(&r, &g);
        r.backend = pass ? gpu3d_backend() : NULL;
        r3d_zclear(&r);
        uint32_t prims = emu_stats.prims, batches = emu_stats.batches, jobs = emu_stats.jobs,
                 zstores = emu_stats.zstores, loads = emu_stats.loads, msframes = emu_stats.msframes,
                 glverts = emu_stats.glverts;
        if (pass)
            gpu3d_drop();               /* each scene as a new cartridge */
        gpu3d_set_msaa(pass == 2);
        scenes[s].fn(&r, &g, pass);
        gpu3d_set_msaa(0);
        if (pass == 2) {
            /* MSAA on any page, or only where the page was cleared */
            const int expect = !emu_ms_load_one || s == CLEARED;
            int differ = 0;
            for (int i = 0; i < w * h; i++)
                differ += c[i] != b[i];
            const float frac = (float)differ / (float)(w * h);
            /* smoothing moves colour across edges but keeps the mean of
             * every small block: blocks of 8x8 whose mean moved */
            int blocks = 0, moved = 0;
            for (int by = 0; by + 8 <= h; by += 8)
                for (int bx = 0; bx + 8 <= w; bx += 8, blocks++) {
                    int sum[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
                    for (int y = by; y < by + 8; y++)
                        for (int x = bx; x < bx + 8; x++)
                            for (int k = 0; k < 3; k++) {
                                sum[0][k] += (int)(rgb(b[y * w + x]) >> (8 * k) & 255);
                                sum[1][k] += (int)(rgb(c[y * w + x]) >> (8 * k) & 255);
                            }
                    for (int k = 0; k < 3; k++)
                        if (abs(sum[0][k] - sum[1][k]) > 16 * 64) {
                            moved++;
                            break;
                        }
                }
            printf("  %-12s MSAA %s: %.2f%% of the pixels smoothed, %d of %d blocks moved\n", scenes[s].name,
                   emu_stats.msframes > msframes ? "on" : "off", frac * 100, moved, blocks);
            if (expect)
                CHECK(emu_stats.msframes > msframes && frac > 0.0005f && moved * 100 <= blocks,
                      "%s: MSAA %u frames, %.2f%% of the pixels changed, %d blocks moved", scenes[s].name,
                      emu_stats.msframes - msframes, frac * 100, moved);
            else
                CHECK(emu_stats.msframes == msframes && differ == 0,
                      "%s: MSAA on a loaded page with one sample loaded", scenes[s].name);
        } else if (pass) {
            CHECK(emu_stats.prims > prims, "%s: the GPU drew nothing", scenes[s].name);
            printf("  %-12s %u triangles in %u batches, %u jobs\n", scenes[s].name, emu_stats.prims - prims,
                   emu_stats.batches - batches, emu_stats.jobs - jobs);
            if (s == 3)
                CHECK(emu_stats.batches - batches >= 2, "many: one batch");
            if (s == 4)
                CHECK(emu_stats.jobs - jobs >= 2, "3 sheets: one job");
            if (s == 5) {
                CHECK(emu_stats.jobs - jobs >= 2, "full job: one job");
                CHECK(emu_stats.zstores > zstores, "full job: the depth was not kept between jobs");
            }
            if (s == 6)
                CHECK(emu_stats.zstores > zstores, "3D 2D 3D: the depth was not kept");
            if (scenes[s].fn == s_vshader || scenes[s].fn == s_vshader_lit ||
                ((scenes[s].fn == s_vshader_clip || scenes[s].fn == s_vshader_heroes) && emu_clip != 1))
                CHECK(emu_stats.glverts > glverts, "vshader: no mesh placed by the vertex shader");
            if (s == CLEARED)
                CHECK(emu_stats.loads == loads && emu_stats.jobs > jobs, "cleared: the page was loaded");
            else
                CHECK(emu_stats.loads > loads, "%s: the page was not loaded", scenes[s].name);
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
    if (s != CLEARED)
        CHECK(rgb(b[5 * w + 5]) == rgb(g16_rgb(200, 200, 0)), "%s: the bar drawn before the 3D is gone",
              scenes[s].name);
    save(a, w, h, scenes[s].name, "cpu");
    save(b, w, h, scenes[s].name, "gpu");
    if (scenes[s].msaa)
        save(c, w, h, scenes[s].name, "msaa");
}

int main(int argc, char **argv)
{
    emu_red_a = argc > 1 ? atoi(argv[1]) : 1;
    emu_tex_swap = argc > 2 ? atoi(argv[2]) : 0;
    emu_tformat = argc > 3 ? atoi(argv[3]) : 0;
    emu_ms_load_one = argc > 4 ? atoi(argv[4]) : 0;
    emu_cw_flip = argc > 5 ? atoi(argv[5]) : 0;
    emu_clip = argc > 6 ? atoi(argv[6]) : 0;
    ppm_dir = argc > 7 ? argv[7] : NULL;
    printf("gpu3d on the emulator: byte a = %s, texels %s, T-format %d, MSAA load %s\n",
           emu_red_a ? "red" : "blue", emu_tex_swap ? "swapped" : "in place", emu_tformat,
           emu_ms_load_one ? "one sample" : "all samples");
    CHECK(gpu3d_init() == 0, "init: %s (%s)", gpu3d_status(), emu_error);
    char want[160];
    static const char *const clips[3] = { "yes", "no", "yes (Z planes)" };
    snprintf(want, sizeof want, "byte a = %s, texels %s, textures in %s, MSAA %s, vertex shader %s, clipping %s, "
             "lit models yes", emu_red_a ? "red" : "blue", emu_tex_swap ? "swapped" : "in place",
             emu_tformat == 2 ? "rows" : "tiles", emu_ms_load_one ? "on cleared pages" : "on any page",
             emu_cw_flip ? "yes" : "yes (cw)", clips[emu_clip]);
    CHECK(strstr(gpu3d_status(), want) != NULL, "probe: '%s', expected '%s'", gpu3d_status(), want);
    if (!gpu3d_ready()) {
        printf("gpu3d: %d/%d checks passed\n", checks - failures, checks);
        return 1;
    }
    make_sheet();
    make_meshes();
    make_hero();
    for (size_t s = 0; s < sizeof scenes / sizeof *scenes; s++)
        run_scene((int)s);
    /* M36: the same lit scenes by the GPU a triangle at a time (r3d places
     * the corners) and with the vertex shader: nearly the same pixels (the
     * clipped corners not quite where the ARM puts them) */
    uint16_t *pg[2] = { test_aligned_alloc(16, 640 * 360 * 2), test_aligned_alloc(16, 640 * 360 * 2) };
    static const char *const sc_name[3] = { "lit", "clipped", "heroes" };
    for (int sc = 0; sc < 3; sc++) {
        scene_fn fn = sc == 2 ? s_vshader_heroes : sc ? s_vshader_clip : s_vshader_lit;
        for (int vsh = 0; vsh < 2; vsh++) {
            g16_t g;
            r3d_t r;
            g16_target(&g, pg[vsh], 640, 640, 360, &font);
            g16_cls(&g, g16_rgb(30, 20, 50));
            r3d_init(&r, &g);
            r.backend = gpu3d_backend();
            gpu3d_drop();
            r3d_zclear(&r);
            vs_off = !vsh;
            fn(&r, &g, 1);
            vs_off = 0;
            r3d_free(&r);
        }
        int differ = 0;
        for (int i = 0; i < 640 * 360; i++)
            differ += pg[0][i] != pg[1][i];
        printf("  vertex shader against r3d's corners (%s): %.3f%% of the pixels differ\n", sc_name[sc],
               differ * 100.0 / (640 * 360));
        CHECK(differ * 1000 < 640 * 360, "vertex shader: %d pixels differ from r3d's corners", differ);
        if (ppm_dir && sc) {
            save(pg[0], 640, 360, sc_name[sc], "nv");
            save(pg[1], 640, 360, sc_name[sc], "gl");
        }
    }
    gpu3d_stats_t st;
    gpu3d_take_stats(&st);
    CHECK(st.jobs >= 5, "%u jobs", st.jobs);
    printf("gpu3d: %u jobs, %u triangles; %d/%d checks passed\n", st.jobs, st.tris, checks - failures, checks);
    return failures ? 1 : 0;
}
