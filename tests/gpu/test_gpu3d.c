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

static uint8_t glyphs[256 * 16];         /* a pattern a glyph (M37's text) */
static const font_t font = { 8, 16, glyphs };
static const char *ppm_dir;

static g16_sheet_t sheet, sheet2, sheet3;
static g16_sheet_t sheets10[10];        /* more sheets than the backend keeps in a job (8) */
static r3d_mesh_t sphere, quad, floor_m, cube, lit_quad, glass, lit_box, hero, hero_tex, hero_skin;
static r3d_mesh_t quad_s, lit_quad_s, lit_box_s;     /* M34: textured screen-door faces */
static const r3d_mesh_t *hero_m = &hero;     /* the hero of s_vshader_heroes */
static float hero_bones[2][12];
static uint8_t hero_vbone[512];
static uint8_t hero_skin_vbone[512];

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
    for (int k = 0; k < 10; k++) {
        g16_sheet_alloc(&sheets10[k], 64, 64);
        for (int i = 0; i < 64 * 64; i++) {
            const int x = i % 64, y = i / 64;
            const uint32_t c = ((x / 8) ^ (y / 8)) & 1 ? 0x101010u * (uint32_t)(k + 4) : 0xF04000u + 0x1814u * (uint32_t)k;
            g16_sheet_set(&sheets10[k], x, y, g16_rgb24(c), 1);
        }
        for (int cy = 0; cy < 8; cy++)
            for (int cx = 0; cx < 8; cx++)
                g16_sheet_update_cell(&sheets10[k], cx, cy);
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
    /* the same with a texture on the ball (Overbit's Meshy heroes): some
     * faces flat, some emissive; the head keeps its colours */
    r3d_mesh_alloc(&hero_tex, hero.nverts, hero.nfaces);
    memcpy(hero_tex.verts, hero.verts, (size_t)hero.nverts * sizeof *hero.verts);
    memcpy(hero_tex.faces, hero.faces, (size_t)hero.nfaces * 3 * sizeof *hero.faces);
    memcpy(hero_tex.colors, hero.colors, (size_t)hero.nfaces * sizeof *hero.colors);
    r3d_mesh_alloc_uv(&hero_tex);
    for (int f = 0; f < s.nfaces; f++) {
        hero_tex.colors[f] = R3D_TEXTURED | (f % 17 == 0 ? R3D_EMISSIVE : 0) | (f % 5 == 0 ? R3D_FLAT : 0);
        for (int k = 0; k < 3; k++) {
            const v3_t p = hero.verts[hero.faces[f * 3 + k]];
            hero_tex.uv[f * 6 + k * 2] = 4 + 120 * (0.5f + 0.5f * p.x);
            hero_tex.uv[f * 6 + k * 2 + 1] = 4 + 120 * (0.5f - 0.5f * p.y);
        }
    }
    hero_tex.tex = &sheet;
    hero_tex.bones = hero.bones;
    hero_tex.vbone = hero_vbone;
    hero_tex.nbones = 2;
    r3d_mesh_normals(&hero_tex);
    /* a skin (the Meshy heroes): the ball's upper half on the head's bone,
     * the faces round its middle on both (vs_lit_tex2) */
    r3d_mesh_alloc(&hero_skin, hero.nverts, hero.nfaces);
    memcpy(hero_skin.verts, hero.verts, (size_t)hero.nverts * sizeof *hero.verts);
    memcpy(hero_skin.faces, hero.faces, (size_t)hero.nfaces * 3 * sizeof *hero.faces);
    memcpy(hero_skin.colors, hero_tex.colors, (size_t)hero.nfaces * sizeof *hero.colors);
    r3d_mesh_alloc_uv(&hero_skin);
    memcpy(hero_skin.uv, hero_tex.uv, (size_t)hero.nfaces * 6 * sizeof *hero_tex.uv);
    for (int i = 0; i < hero.nverts; i++)
        hero_skin_vbone[i] = (uint8_t)(i < s.nverts ? hero.verts[i].y > 0.05f : 1);
    hero_skin.tex = &sheet;
    hero_skin.bones = hero.bones;
    hero_skin.vbone = hero_skin_vbone;
    hero_skin.nbones = 2;
    r3d_mesh_normals(&hero_skin);
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
    /* M34: the same with their textured faces screen-door (the second row
     * of the sheet: transparent texels too) */
    const r3d_mesh_t *src[3] = { &quad, &lit_quad, &lit_box };
    r3d_mesh_t *dst[3] = { &quad_s, &lit_quad_s, &lit_box_s };
    for (int k = 0; k < 3; k++) {
        r3d_mesh_alloc(dst[k], src[k]->nverts, src[k]->nfaces);
        memcpy(dst[k]->verts, src[k]->verts, (size_t)src[k]->nverts * sizeof *dst[k]->verts);
        memcpy(dst[k]->faces, src[k]->faces, (size_t)src[k]->nfaces * 3 * sizeof *dst[k]->faces);
        memcpy(dst[k]->colors, src[k]->colors, (size_t)src[k]->nfaces * sizeof *dst[k]->colors);
        r3d_mesh_alloc_uv(dst[k]);
        memcpy(dst[k]->uv, src[k]->uv, (size_t)src[k]->nfaces * 6 * sizeof *dst[k]->uv);
        for (int f = 0; f < src[k]->nfaces; f++)
            if (dst[k]->colors[f] & R3D_TEXTURED) {
                dst[k]->colors[f] |= R3D_SCREEN;
                for (int i = 1; i < 6; i += 2)
                    dst[k]->uv[f * 6 + i] += k == 0 ? 0 : 32;      /* the row with holes */
            }
        if (src[k]->clight) {
            dst[k]->clight = malloc((size_t)src[k]->nfaces * 9);
            memcpy(dst[k]->clight, src[k]->clight, (size_t)src[k]->nfaces * 9);
        }
        dst[k]->tex = &sheet;
        r3d_mesh_normals(dst[k]);
    }
}

/* ---------------------------------------------------------------- scenes */

typedef void (*scene_fn)(r3d_t *r, g16_t *g, int gpu);

static int use_queue;                   /* M35: the end of the scene started, then waited for */

static void flush(r3d_t *r, g16_t *g, int gpu)
{
    (void)r;
    if (gpu && use_queue) {
        CHECK(gpu3d_submit(g, 0) == 0 && gpu3d_sync() == 0, "submit: %s (%s)", gpu3d_status(), emu_error);
        return;
    }
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

/* M39: "lit" boxes in a row from far to near, each hiding most of the one
 * before: drawn as the scene says or nearest first (gpu3d_set_sort) */
static void s_far_to_near(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0.5f, -5, 0, -0.1f, 60);
    r3d_light(r, -0.3f, 0.8f, 0.4f, 0.4f);
    r3d_fog(r, 0xC0A080, 3, 14);
    gpu3d_set_vshader(gpu != 0);
    for (int i = 0; i < 8; i++)
        r3d_draw_flags(r, &lit_box, (v3_t){ 0.15f * (float)(i & 1), 0, 9.0f - (float)i }, 0.4f, 0.7f + 0.1f * (float)i,
                       0, 1.6f, 0);
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
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
            r3d_draw_flags(r, hero_m, (v3_t){ at[i].x, -1.2f, at[i].z }, 0, 0.3f * (float)i, 0, 0.7f, R3D_SHADOW);
        r3d_draw_flags(r, hero_m, at[i], 0, 0.3f * (float)i, 0, 0.7f, i == 2 ? 0 : R3D_SMOOTH);
    }
    /* a first-person model: in front of everything drawn before */
    r3d_draw_flags(r, hero_m, (v3_t){ 0.9f, -0.2f, -3.2f }, 0.3f, 0.5f, 0, 0.3f, R3D_FRONT | R3D_SMOOTH);
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
    r3d_lamp(r, 0, 0, 0, 0, 0, 0);
    r3d_fog(r, 0, 0, 0);
    r3d_shine(r, 0.6f, 16, 0);              /* as r3d_init */
    r3d_sky(r, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
}

/* the same with textured heroes: vs_lit_tex (grey light at the corners,
 * Gouraud or flat, emissive faces) */
static void s_vshader_heroes_tex(r3d_t *r, g16_t *g, int gpu)
{
    hero_m = &hero_tex;
    s_vshader_heroes(r, g, gpu);
    hero_m = &hero;
}

/* and as a skin: faces on two bones (vs_lit_tex2, vs_shadow2) */
static void s_vshader_skin(r3d_t *r, g16_t *g, int gpu)
{
    hero_m = &hero_skin;
    s_vshader_heroes(r, g, gpu);
    hero_m = &hero;
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

/* ten sheets in turn: more than a job keeps (8), so two jobs */
static void s_sheets10(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0, -6, 0, 0, 60);
    r3d_light(r, 0, 0, -1, 0.6f);
    for (int i = 0; i < 10; i++) {
        quad.tex = &sheets10[i];
        r3d_draw_flags(r, &quad, (v3_t){ -3.2f + 0.7f * (float)(i % 5), i < 5 ? 0.7f : -0.7f, 0 }, 0, 0.1f, 0,
                       0.5f, 0);
    }
    quad.tex = &sheet;
    flush(r, g, gpu);
}

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

/* M34: textured screen-door faces over a sphere: a plain textured quad, a
 * quad of a "lit" model (light baked, fog), a "lit" box through the vertex
 * shader (vsh), all on the GPU (they went to the ARM before) */
static void s_tex_screen(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0.3f, -5, 0, -0.05f, 60);
    r3d_light(r, -0.3f, 0.8f, -0.4f, 0.4f);
    r3d_draw_flags(r, &sphere, (v3_t){ 0, 0, 3 }, 0, 0, 0, 2.2f, R3D_SMOOTH);
    r3d_draw_flags(r, &quad_s, (v3_t){ -1.8f, 0.6f, 0 }, 0, 0.3f, 0, 0.9f, 0);
    r3d_fog(r, 0xC0A080, 3, 12);
    r3d_draw_flags(r, &lit_quad_s, (v3_t){ 0.3f, 0.8f, 0.5f }, 0, -0.2f, 0, 0.9f, 0);
    gpu3d_set_vshader(gpu != 0);
    r3d_draw_flags(r, &lit_box_s, (v3_t){ 1.5f, -0.6f, 0.5f }, 0.4f, 0.7f, 0, 0.9f, 0);
    r3d_fog(r, 0, 0, 0);
    if (gpu)
        CHECK(r->backend != NULL, "tex screen: the textured screen-door faces went to the ARM");
    flush(r, g, gpu);
    gpu3d_set_vshader(0);
}

/* M37: 2D over the 3D in the same job: rectangles, sprites (flipped, at
 * twice their size), text (in lines, scaled), under a camera and a clip
 * rectangle, then 3D partly behind it. The ARM draws the same with gfx16:
 * the 2D pixel for pixel, the 3D after it hiding it where it is nearer. */
static int g2d;                         /* the 2D drawn by the GPU */
static void rect2d(g16_t *g, int x, int y, int w, int h, uint16_t c)
{
    if (g2d)
        CHECK(gpu3d_rect2d(g, x - g->cam_x, y - g->cam_y, x - g->cam_x + w, y - g->cam_y + h, c), "rect2d");
    else
        g16_rectfill(g, x, y, w, h, c);
}

static void spr2d(g16_t *g, int sx, int sy, int sw, int sh, int dx, int dy, int fx, int fy, int zoom)
{
    if (g2d)
        CHECK(gpu3d_blit2d(g, &sheet, sx, sy, sw, sh, dx - g->cam_x, dy - g->cam_y, zoom, fx, fy), "blit2d");
    else if (zoom == 1)
        g16_sspr(g, &sheet, sx, sy, sw, sh, dx, dy, fx, fy);
    else
        g16_sspr_zoom(g, &sheet, sx, sy, sw, sh, dx, dy, fx, fy, (float)zoom);
}

static void text2d(g16_t *g, int x, int y, const char *t, uint16_t c, int scale)
{
    if (g2d)
        CHECK(gpu3d_text2d(g, x, y, t, c, scale), "text2d");
    else
        g16_text_scaled(g, x, y, t, c, scale);
}

static void s_2d(r3d_t *r, g16_t *g, int gpu)
{
    r3d_camera(r, 0, 0, -6, 0, 0, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_draw_flags(r, &sphere, (v3_t){ -0.5f, 0, 1 }, 0, 0.3f, 0, 1.6f, R3D_SMOOTH);
    g2d = gpu;
    g->cam_x = 7;
    g->cam_y = -5;
    g->cx0 = 16; g->cy0 = 20; g->cx1 = 600; g->cy1 = 330;
    rect2d(g, 0, 0, 200, 40, g16_rgb(20, 60, 200));              /* cut by the clip rectangle */
    rect2d(g, 300, 150, 90, 60, g16_rgb(250, 120, 30));
    rect2d(g, 590, 300, 50, 50, g16_rgb(60, 220, 90));           /* out on the right and below */
    spr2d(g, 0, 32, 32, 32, 40, 60, 0, 0, 1);                    /* transparent texels */
    spr2d(g, 0, 32, 32, 32, 80, 60, 1, 0, 1);                    /* flipped */
    spr2d(g, 32, 0, 32, 32, 120, 60, 0, 1, 1);
    spr2d(g, 64, 32, 32, 32, 170, 50, 1, 1, 2);                  /* twice as big */
    spr2d(g, 96, 0, 24, 16, 8, 200, 0, 0, 3);                    /* three times, cut on the left */
    text2d(g, 40, 120, "HUD 250/250\nline two", g16_rgb(255, 255, 255), 1);
    text2d(g, 300, 240, "BIG\nA", g16_rgb(255, 230, 40), 2);
    g->cam_x = g->cam_y = 0;
    g->cx0 = g->cy0 = 0; g->cx1 = g->w; g->cy1 = g->h;
    g2d = 0;
    /* the 3D after the 2D: in front of the sphere, partly over the 2D */
    r3d_draw_flags(r, &cube, (v3_t){ 1.2f, 0.4f, -0.5f }, 0.3f, 0.6f, 0, 1.0f, 0);
    r3d_draw_flags(r, &sphere, (v3_t){ -0.7f, 0.2f, 3 }, 0, 0, 0, 1.2f, 0);     /* behind the first */
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
    { "vshader textured", s_vshader_heroes_tex, 640, 360, 0.04f, 1 },
    { "vshader skin", s_vshader_skin, 640, 360, 0.04f, 1 },
    { "10 sheets", s_sheets10, 640, 360, 0.02f, 0 },
    { "tex screen", s_tex_screen, 640, 360, 0.03f, 0 },
    { "2D on GPU", s_2d, 640, 360, 0.02f, 0 },
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
                CHECK(emu_stats.jobs - jobs == 1, "3 sheets: %u jobs (8 sheets fit in one)", emu_stats.jobs - jobs);
            if (scenes[s].fn == s_sheets10)
                CHECK(emu_stats.jobs - jobs >= 2, "10 sheets: one job");
            if (s == 5) {
                CHECK(emu_stats.jobs - jobs >= 2, "full job: one job");
                CHECK(emu_stats.zstores > zstores, "full job: the depth was not kept between jobs");
            }
            if (s == 6)
                CHECK(emu_stats.zstores > zstores, "3D 2D 3D: the depth was not kept");
            if (scenes[s].fn == s_vshader || scenes[s].fn == s_vshader_lit ||
                ((scenes[s].fn == s_vshader_clip || scenes[s].fn == s_vshader_heroes ||
                  scenes[s].fn == s_vshader_heroes_tex || scenes[s].fn == s_vshader_skin) && emu_clip != 1))
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
    emu_hang_zclear = getenv("EMU_HANG_ZCLEAR") != NULL;     /* the probe's job does not end */
    emu_vpm_words = getenv("EMU_VPM_WORDS") != NULL;         /* GL records in words: the probe tries bytes first */
    emu_no_threads = getenv("EMU_NO_THREADS") != NULL;       /* the two-thread shaders' probe: its job does not end */
    ppm_dir = argc > 7 ? argv[7] : NULL;
    printf("gpu3d on the emulator: byte a = %s, texels %s, T-format %d, MSAA load %s\n",
           emu_red_a ? "red" : "blue", emu_tex_swap ? "swapped" : "in place", emu_tformat,
           emu_ms_load_one ? "one sample" : "all samples");
    CHECK(gpu3d_init() == 0, "init: %s (%s)", gpu3d_status(), emu_error);
    char want[320];
    static const char *const clips[3] = { "yes", "no", "yes (Z planes)" };
    snprintf(want, sizeof want, "byte a = %s, texels %s, textures in %s, MSAA %s, 16-bit textures %s, "
             "two-thread shaders %s, vertex shader %s, "
             "indexed yes, clipping %s, "
             "lit models yes, queue yes, zclear in job %s", emu_red_a ? "red" : "blue", emu_tex_swap ? "swapped" : "in place",
             emu_tformat == 2 ? "rows" : "tiles", emu_ms_load_one ? "on cleared pages" : "on any page",
             emu_tformat == 2 ? "no" : "yes", emu_no_threads ? "no" : "yes",
             emu_vpm_words ? (emu_cw_flip ? "yes (VPM in words)" : "yes (cw, VPM in words)")
                           : emu_cw_flip ? "yes" : "yes (cw)", clips[emu_clip], emu_hang_zclear ? "no" : "yes");
    CHECK(strstr(gpu3d_status(), want) != NULL, "probe: '%s', expected '%s'", gpu3d_status(), want);
    if (!gpu3d_ready()) {
        printf("gpu3d: %d/%d checks passed\n", checks - failures, checks);
        return 1;
    }
    for (int i = 0; i < 256 * 16; i++)
        glyphs[i] = (uint8_t)((i / 16) * 37 + (i % 16) * 11 + ((i % 16) & 1 ? 0x81 : 0x18));
    make_sheet();
    make_meshes();
    make_hero();
    for (size_t s = 0; s < sizeof scenes / sizeof *scenes; s++)
        run_scene((int)s);
    /* M36: the same lit scenes by the GPU a triangle at a time (r3d places
     * the corners) and with the vertex shader: nearly the same pixels (the
     * clipped corners not quite where the ARM puts them) */
    uint16_t *pg[2] = { test_aligned_alloc(16, 640 * 360 * 2), test_aligned_alloc(16, 640 * 360 * 2) };
    static const char *const sc_name[5] = { "lit", "clipped", "heroes", "textured", "skin" };
    for (int sc = 0; sc < 5; sc++) {
        scene_fn fn = sc == 4 ? s_vshader_skin : sc == 3 ? s_vshader_heroes_tex : sc == 2 ? s_vshader_heroes
                    : sc ? s_vshader_clip : s_vshader_lit;
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
        /* the textured faces: the light halved and doubled by vs_lit_tex
         * and fs_tex_rgb, a step or two of a byte off fs_tex_lit's */
        int differ = 0;
        for (int i = 0; i < 640 * 360; i++) {
            if (sc < 3) {
                differ += pg[0][i] != pg[1][i];
                continue;
            }
            const uint32_t ca = rgb(pg[0][i]), cb = rgb(pg[1][i]);
            int d = 0;
            for (int k = 0; k < 24; k += 8) {
                const int x = (int)(ca >> k & 255) - (int)(cb >> k & 255);
                d |= x > 12 || x < -12;
            }
            differ += d;
        }
        printf("  vertex shader against r3d's corners (%s): %.3f%% of the pixels differ\n", sc_name[sc],
               differ * 100.0 / (640 * 360));
        CHECK(differ * 1000 < 640 * 360, "vertex shader: %d pixels differ from r3d's corners", differ);
        if (ppm_dir && sc) {
            save(pg[0], 640, 360, sc_name[sc], "nv");
            save(pg[1], 640, 360, sc_name[sc], "gl");
        }
    }
    /* M39: opaque sheets as RGB565 textures: the same pixels as 32-bit */
    {
        uint16_t *q[2] = { test_aligned_alloc(16, 640 * 360 * 2), test_aligned_alloc(16, 640 * 360 * 2) };
        for (int k = 0; k < 2; k++) {
            g16_t g;
            r3d_t r;
            g16_target(&g, q[k], 640, 640, 360, &font);
            g16_cls(&g, g16_rgb(30, 20, 50));
            r3d_init(&r, &g);
            r.backend = gpu3d_backend();
            gpu3d_drop();
            r3d_zclear(&r);
            gpu3d_set_tex16(k);
            s_sheets10(&r, &g, 1);
            CHECK(!k || gpu3d_tex16() == (emu_tformat != 2), "16-bit textures: not on");
            gpu3d_set_tex16(0);
            r3d_free(&r);
        }
        int differ = 0;
        for (int i = 0; i < 640 * 360; i++)
            differ += q[0][i] != q[1][i];
        printf("  RGB565 textures: %d pixels differ from 32-bit ones\n", differ);
        CHECK(differ == 0, "16-bit textures: %d pixels differ", differ);
    }
    /* M39: the opaque meshes nearest first: the same picture, fewer
     * pixels shaded (the early z throws away those of the boxes behind) */
    {
        uint16_t *q[2] = { test_aligned_alloc(16, 640 * 360 * 2), test_aligned_alloc(16, 640 * 360 * 2) };
        uint32_t shaded[2], moved = 0;
        for (int k = 0; k < 2; k++) {
            g16_t g;
            r3d_t r;
            g16_target(&g, q[k], 640, 640, 360, &font);
            g16_cls(&g, g16_rgb(30, 20, 50));
            r3d_init(&r, &g);
            r.backend = gpu3d_backend();
            gpu3d_drop();
            r3d_zclear(&r);
            gpu3d_set_sort(k);
            gpu3d_stats_t st;
            gpu3d_take_stats(&st);
            const uint32_t p0 = emu_stats.pixels;
            s_far_to_near(&r, &g, 1);
            shaded[k] = emu_stats.pixels - p0;
            gpu3d_take_stats(&st);
            if (k)
                moved = st.sorted;
            gpu3d_set_sort(0);
            r3d_free(&r);
        }
        int differ = 0;
        for (int i = 0; i < 640 * 360; i++)
            differ += q[0][i] != q[1][i];
        printf("  nearest first: %u pixels shaded instead of %u, %u draws moved, %d pixels differ\n", shaded[1],
               shaded[0], moved, differ);
        CHECK(differ == 0 && moved >= 7 && shaded[1] * 2 < shaded[0],
              "nearest first: %d pixels differ, %u draws moved, %u pixels shaded against %u", differ, moved,
              shaded[1], shaded[0]);
        test_free(q[0]);
        test_free(q[1]);
    }
    /* M39: the textured faces' shaders with two threads: the same pixels
     * (sheets, a lit quad in the fog, textured heroes by the vertex shader) */
    {
        uint16_t *q[2] = { test_aligned_alloc(16, 640 * 360 * 2), test_aligned_alloc(16, 640 * 360 * 2) };
        static const scene_fn fns[3] = { s_sheets10, s_overbit, s_vshader_heroes_tex };
        int differ = 0;
        uint32_t threaded = 0;
        for (int f = 0; f < 3; f++)
            for (int k = 0; k < 2; k++) {
                g16_t g;
                r3d_t r;
                g16_target(&g, q[k], 640, 640, 360, &font);
                g16_cls(&g, g16_rgb(30, 20, 50));
                r3d_init(&r, &g);
                r.backend = gpu3d_backend();
                gpu3d_drop();
                r3d_zclear(&r);
                gpu3d_set_fs2(k);
                const uint32_t t0 = emu_stats.threaded;
                fns[f](&r, &g, 1);
                if (k)
                    threaded += emu_stats.threaded - t0;
                else
                    CHECK(emu_stats.threaded == t0, "two-thread shaders: drawn while off");
                CHECK(!k || gpu3d_fs2() == !emu_no_threads, "two-thread shaders: on %d", gpu3d_fs2());
                gpu3d_set_fs2(0);
                r3d_free(&r);
                if (k)
                    for (int i = 0; i < 640 * 360; i++)
                        differ += q[0][i] != q[1][i];
            }
        printf("  two-thread shaders: %u batches with them, %d pixels differ\n", threaded, differ);
        CHECK(differ == 0 && (emu_no_threads ? threaded == 0 : threaded >= 3),
              "two-thread shaders: %d pixels differ, %u batches", differ, threaded);
        test_free(q[0]);
        test_free(q[1]);
    }
    /* M37: the textures filtered (bilinear): the same scene changes, but
     * only a little (the magnified floor smooths its squares' edges) */
    {
        uint16_t *q[2] = { test_aligned_alloc(16, 640 * 360 * 2), test_aligned_alloc(16, 640 * 360 * 2) };
        for (int k = 0; k < 2; k++) {
            g16_t g;
            r3d_t r;
            g16_target(&g, q[k], 640, 640, 360, &font);
            g16_cls(&g, g16_rgb(30, 20, 50));
            r3d_init(&r, &g);
            r.backend = gpu3d_backend();
            gpu3d_drop();
            r3d_zclear(&r);
            gpu3d_set_bilinear(k);
            s_textures(&r, &g, 1);
            gpu3d_set_bilinear(0);
            r3d_free(&r);
        }
        int differ = 0;
        for (int i = 0; i < 640 * 360; i++)
            differ += q[0][i] != q[1][i];
        printf("  bilinear textures: %.2f%% of the pixels differ from the nearest texel\n", differ * 100.0 / (640 * 360));
        CHECK(differ > 200 && differ < 640 * 360 / 4, "bilinear: %d pixels differ from the nearest texel", differ);
        save(q[1], 640, 360, "textures", "bilinear");
    }
    /* M37: a frame enlarged by the GPU (the menu at 1080p): 3 times, the
     * nearest pixel, as the ARM enlarges it */
    {
        enum { FW = 160, FH = 90, K = 3 };
        static uint16_t src[FW * FH];
        for (int i = 0; i < FW * FH; i++)
            src[i] = (uint16_t)(i * 2654435761u >> 16);
        uint16_t *big = test_aligned_alloc(64, FW * K * FH * K * 2);
        g16_t pg;
        g16_target(&pg, big, FW * K, FW * K, FH * K, &font);
        CHECK(gpu3d_enlarge(src, FW, FH, K, &pg) == 0, "enlarge: %s (%s)", gpu3d_status(), emu_error);
        int differ = 0;
        for (int y = 0; y < FH * K; y++)
            for (int x = 0; x < FW * K; x++)
                differ += big[y * FW * K + x] != src[(y / K) * FW + x / K];
        printf("  enlarged %dx%d %d times by the GPU: %d pixels differ\n", FW, FH, K, differ);
        CHECK(differ == 0, "enlarge: %d pixels differ from the nearest", differ);
    }
    /* M39: two jobs in flight (gpu3d_set_queue(2)): a frame's job started
     * and not waited for, the next frame's filled in the other block on
     * another page meanwhile, then both: the pictures of one at a time (the
     * emulator runs a started job when it is waited for: memory the driver
     * reused under it would show) */
    {
        gpu3d_stats_t s0;
        gpu3d_take_stats(&s0);
        static const int sc[3] = { 0, 1, 8 };           /* spheres, textures, overbit */
        uint16_t *one[3], *two[3];
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k < 3; k++) {
                const int si = sc[k], w = scenes[si].w, h = scenes[si].h;
                uint16_t *pgk = test_aligned_alloc(16, (size_t)w * h * 2);
                (pass ? two : one)[k] = pgk;
                g16_t g;
                r3d_t r;
                g16_target(&g, pgk, (uint32_t)w, w, h, &font);
                g16_cls(&g, g16_rgb(30, 20, 50));
                r3d_init(&r, &g);
                r.backend = gpu3d_backend();
                if (k == 0)
                    gpu3d_drop();
                r3d_zclear(&r);
                gpu3d_set_queue(pass ? 2 : 0);
                use_queue = pass;
                if (pass) {
                    scenes[si].fn(&r, &g, 0);       /* the triangles into the job, ... */
                    CHECK(gpu3d_submit(&g, 0) == 0, "two jobs: submit %s", emu_error);   /* ... started */
                } else {
                    scenes[si].fn(&r, &g, 1);
                }
                use_queue = 0;
                r3d_free(&r);
            }
            CHECK(gpu3d_sync() == 0, "two jobs: %s", emu_error);
            gpu3d_set_queue(0);
        }
        int differ = 0;
        for (int k = 0; k < 3; k++)
            for (int i = 0; i < scenes[sc[k]].w * scenes[sc[k]].h; i++)
                differ += one[k][i] != two[k][i];
        gpu3d_take_stats(&s0);
        printf("  two jobs in flight: %u jobs filled while the one before was drawn, %d pixels differ from one job "
               "at a time\n", s0.overlapped, differ);
        CHECK(differ == 0 && gpu3d_queue2() == 0 && s0.overlapped >= 2, "two jobs in flight: %d pixels differ, %u "
              "overlapped", differ, s0.overlapped);
    }
    /* M35: every scene again with its end started on the V3D (semaphores,
     * gpu3d_submit) and waited for, its zclear()s inside the job (fs_zclear):
     * the same pixels */
    gpu3d_stats_t st0;
    gpu3d_take_stats(&st0);
    for (size_t s = 0; s < sizeof scenes / sizeof *scenes; s++) {
        const int w = scenes[s].w, h = scenes[s].h;
        uint16_t *q[2] = { test_aligned_alloc(16, (size_t)w * h * 2), test_aligned_alloc(16, (size_t)w * h * 2) };
        const uint32_t async = emu_stats.async;
        for (int k = 0; k < 2; k++) {
            g16_t g;
            r3d_t r;
            g16_target(&g, q[k], (uint32_t)w, w, h, &font);
            g16_cls(&g, g16_rgb(30, 20, 50));
            r3d_init(&r, &g);
            r.backend = gpu3d_backend();
            gpu3d_drop();
            r3d_zclear(&r);
            use_queue = k;
            gpu3d_set_queue(k);
            scenes[s].fn(&r, &g, 1);
            gpu3d_set_queue(0);
            use_queue = 0;
            r3d_free(&r);
        }
        int differ = 0;
        for (int i = 0; i < w * h; i++)
            differ += q[0][i] != q[1][i];
        CHECK(differ == 0, "queue: %s, %d pixels differ from the job run to its end", scenes[s].name, differ);
        CHECK(emu_stats.async > async, "queue: %s, no job started", scenes[s].name);
    }
    gpu3d_stats_t st;
    gpu3d_take_stats(&st);
    printf("  queue: %u jobs started and waited for later, %u zclear() inside a job, the same pixels\n",
           emu_stats.async, st.zinjob);
    CHECK(emu_hang_zclear ? st.zinjob == 0 : st.zinjob >= 2,
          "queue: %u zclear() inside a job (the first-person and noz_zclear scenes)", st.zinjob);
    st.jobs += st0.jobs;
    st.tris += st0.tris;
    CHECK(st.jobs >= 5, "%u jobs", st.jobs);
    printf("  vertex shader: %u corners shaded for %u indexed (M39)\n", emu_stats.glverts, emu_stats.glindexed);
    CHECK(emu_stats.glindexed > 0 && emu_stats.glverts < emu_stats.glindexed, "indexed meshes: %u shaded for %u",
          emu_stats.glverts, emu_stats.glindexed);
    printf("gpu3d: %u jobs, %u triangles; %d/%d checks passed\n", st.jobs, st.tris, checks - failures, checks);
    return failures ? 1 : 0;
}
