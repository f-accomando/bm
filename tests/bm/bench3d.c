/*
 * Rasterizer bench (M30): fixed 3D scenes drawn with src/bm/r3d.c.
 *
 *   bench3d                 every scene: checksum of the picture, pixels
 *                           written, triangles drawn
 *   bench3d SCENE [N]       only that scene, N frames (for instruction
 *                           counts: tests/bm/count_insns.py runs the ARM
 *                           build under qemu-arm)
 *   bench3d -ppm DIR        also writes DIR/SCENE.ppm
 *
 * Built with -DBENCH_GPU (and tests/gpu/v3d_emu.c, src/gpu/gpu3d.c), a
 * scene named SCENE+gpu is drawn by the GPU backend on the V3D emulator:
 * the ARM's share of the 3D with the GPU (M30).
 *
 * The checksums tell whether a change to the rasterizer changes the
 * picture; the ARM instruction counts tell what it costs, per pixel and
 * per triangle, without the Pi (cache and bus not included).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static uint16_t fb_ram[640 * 360];
static uint16_t *fb = fb_ram;           /* with the GPU: memory the emulated V3D reaches */
static g16_t g;
static r3d_t r;
static g16_sheet_t sheet128, sheet256;

/* ---------------------------------------------------------------- data */

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

/* 128x128: 4x4 textures of 32x32, like Texture Room; the last one has a
 * transparent border */
static void make_sheets(void)
{
    g16_sheet_alloc(&sheet128, 128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int t = (y / 32) * 4 + x / 32, lx = x & 31, ly = y & 31;
            uint32_t h = hash((uint32_t)(x * 131 + y * 7919));
            uint32_t base = hash((uint32_t)t * 977u);
            uint32_t rr = (base & 0xFF) / 2 + (h & 63), gg = (base >> 8 & 0xFF) / 2 + (h >> 8 & 63),
                     bb = (base >> 16 & 0xFF) / 2 + (h >> 16 & 63);
            if ((lx & 7) == 0 || (ly & 15) == 0) { rr /= 2; gg /= 2; bb /= 2; }
            int opaque = !(t == 15 && (lx < 3 || lx > 28));
            g16_sheet_set(&sheet128, x, y, g16_rgb(rr, gg, bb), opaque);
        }
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 16; cx++)
            g16_sheet_update_cell(&sheet128, cx, cy);
    g16_sheet_alloc(&sheet256, 256, 256);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            g16_sheet_set(&sheet256, x, y, g16_rgb((uint32_t)(x ^ y), (uint32_t)(x * 3 + y) & 255,
                                                   (uint32_t)(y * 5) & 255), 1);
    for (int cy = 0; cy < 32; cy++)
        for (int cx = 0; cx < 32; cx++)
            g16_sheet_update_cell(&sheet256, cx, cy);
}

/* mesh builder: textured quads, corners clockwise as seen from the front
 * (bottom left, top left, top right, bottom right), as in Texture Room */
typedef struct { float v[3 * 4096]; uint16_t f[3 * 4096]; float uv[6 * 4096]; int nv, nf; } builder_t;
static builder_t B;

static void b_quad(const float *p1, const float *p2, const float *p3, const float *p4, int t)
{
    float u0 = (t % 4) * 32 + 0.5f, v0 = (t / 4) * 32 + 0.5f, u1 = u0 + 31, v1 = v0 + 31;
    int i = B.nv;
    const float *p[4] = { p1, p2, p3, p4 };
    for (int k = 0; k < 4; k++)
        memcpy(&B.v[(B.nv++) * 3], p[k], 3 * sizeof(float));
    static const int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
    const float uvs[2][6] = { { u0, v1, u0, v0, u1, v0 }, { u0, v1, u1, v0, u1, v1 } };
    for (int k = 0; k < 2; k++) {
        for (int j = 0; j < 3; j++)
            B.f[B.nf * 3 + j] = (uint16_t)(i + tri[k][j]);
        memcpy(&B.uv[B.nf * 6], uvs[k], sizeof uvs[k]);
        B.nf++;
    }
}

/* as the runtime's mesh(): faces and uv in the order given */
static void b_build(r3d_mesh_t *m, const g16_sheet_t *tex)
{
    r3d_mesh_alloc(m, B.nv, B.nf);
    memcpy(m->verts, B.v, (size_t)B.nv * sizeof(v3_t));
    memcpy(m->faces, B.f, (size_t)B.nf * 3 * sizeof(uint16_t));
    for (int t = 0; t < B.nf; t++)
        m->colors[t] = R3D_TEXTURED;
    r3d_mesh_alloc_uv(m);
    memcpy(m->uv, B.uv, (size_t)B.nf * 6 * sizeof(float));
    m->tex = tex;
    r3d_mesh_normals(m);
    B.nv = B.nf = 0;
}

static r3d_mesh_t floor_m, walls_m, crate_m, pillar_m, quad_m, quadtex_m, sphere_m, spheretex_m;

static void make_room(void)
{
    const int ROOM = 12;
    const float WALL_H = 5;
    for (int i = -ROOM; i <= ROOM - 2; i += 2)
        for (int j = -ROOM; j <= ROOM - 2; j += 2) {
            int t = ((i * 7 + j * 13) % 11 == 0) ? 4 : 0;
            float p1[3] = { (float)i, 0, (float)j }, p2[3] = { (float)i, 0, (float)(j + 2) },
                  p3[3] = { (float)(i + 2), 0, (float)(j + 2) }, p4[3] = { (float)(i + 2), 0, (float)j };
            b_quad(p1, p2, p3, p4, t);
        }
    b_build(&floor_m, &sheet128);
    for (int side = 0; side < 4; side++)
        for (int i = -ROOM; i <= ROOM - 2; i += 2)
            for (float y = 0; y <= WALL_H - 1; y += 2.5f) {
                float q[4][3] = { { (float)i, y, (float)ROOM }, { (float)i, y + 2.5f, (float)ROOM },
                                  { (float)(i + 2), y + 2.5f, (float)ROOM }, { (float)(i + 2), y, (float)ROOM } };
                for (int k = 0; k < 4; k++) {
                    float x = q[k][0], z = q[k][2];
                    if (side == 1) { q[k][0] = z; q[k][2] = -x; }
                    if (side == 2) { q[k][0] = -x; q[k][2] = -z; }
                    if (side == 3) { q[k][0] = -z; q[k][2] = x; }
                }
                b_quad(q[0], q[1], q[2], q[3], y == 0 ? 6 : 1);
            }
    b_build(&walls_m, &sheet128);
    static const float c[8][3] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
                                   { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    static const int cf[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 3, 7, 6, 2 },
                                  { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
    for (int k = 0; k < 6; k++)
        b_quad(c[cf[k][0]], c[cf[k][1]], c[cf[k][2]], c[cf[k][3]], 2);
    b_build(&crate_m, &sheet128);
    const float s = 0.6f;
    const float p[8][3] = { { -s, 0, -s }, { s, 0, -s }, { s, WALL_H, -s }, { -s, WALL_H, -s },
                            { -s, 0, s }, { s, 0, s }, { s, WALL_H, s }, { -s, WALL_H, s } };
    static const int pf[4][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
    for (int k = 0; k < 4; k++)
        b_quad(p[pf[k][0]], p[pf[k][1]], p[pf[k][2]], p[pf[k][3]], 3);
    b_build(&pillar_m, &sheet128);
}

static void make_quads(void)
{
    r3d_mesh_t *q[2] = { &quad_m, &quadtex_m };
    for (int k = 0; k < 2; k++) {
        r3d_mesh_alloc(q[k], 4, 2);
        q[k]->verts[0] = (v3_t){ -1, -0.5625f, 0 }; q[k]->verts[1] = (v3_t){ 1, -0.5625f, 0 };
        q[k]->verts[2] = (v3_t){ 1, 0.5625f, 0 };   q[k]->verts[3] = (v3_t){ -1, 0.5625f, 0 };
        static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
        memcpy(q[k]->faces, f, sizeof f);
        q[k]->colors[0] = q[k]->colors[1] = 0x80C0FF;
        r3d_mesh_normals(q[k]);
    }
    r3d_mesh_alloc_uv(&quadtex_m);
    static const float uv[12] = { 0, 256, 256, 0, 256, 256,   0, 256, 0, 0, 256, 0 };
    memcpy(quadtex_m.uv, uv, sizeof uv);
    quadtex_m.colors[0] = quadtex_m.colors[1] = R3D_TEXTURED;
    quadtex_m.tex = &sheet256;
    /* the stress test's sphere, and a textured one (32x32 texture 2) */
    r3d_mesh_sphere(&sphere_m, 6, 8, 0x4080FF, 0xFFC040);
    r3d_mesh_sphere(&spheretex_m, 6, 8, 0, 0);
    r3d_mesh_alloc_uv(&spheretex_m);
    for (int t = 0; t < spheretex_m.nfaces; t++) {
        spheretex_m.colors[t] = R3D_TEXTURED;
        for (int i = 0; i < 3; i++) {
            int v = spheretex_m.faces[t * 3 + i];
            spheretex_m.uv[t * 6 + i * 2] = 64 + (v % 8) * 4.0f;
            spheretex_m.uv[t * 6 + i * 2 + 1] = (v / 8) * 5.0f;
        }
    }
    spheretex_m.tex = &sheet128;
}

/* ---------------------------------------------------------------- scenes */

static void target(int w, int h)
{
    g16_target(&g, fb, (uint32_t)w, w, h, &font);
    r.g = &g;
    free(r.zbuf);
    r.zbuf = malloc((size_t)w * h * 2);
    r3d_camera(&r, 0, 0, 0, 0, 0, 60);
}

/* four quads of 320x180 on the four quadrants, nearer each time */
static void quads(r3d_mesh_t *m, unsigned flags)
{
    target(640, 360);
    g16_cls(&g, g16_rgb(10, 10, 30));
    r3d_zclear(&r);
    r3d_camera(&r, 0, 0, 0, 0, 0, 60);
    r3d_light(&r, 0.3f, 0.4f, -1, 0.3f);
    if (flags & R3D_SMOOTH)
        r3d_lamp(&r, 0, -4, 3, 36, 20, 1.5f);
    const float k = 160.0f / r.focal;
    float d = 40;
    for (int i = 0; i < 4; i++, d *= 0.985f) {
        float x = (i & 1) ? k * d : -k * d, y = (i & 2) ? -0.5625f * k * d : 0.5625f * k * d;
        r3d_draw_flags(&r, m, (v3_t){ x, y, d }, 0, 0, 0, k * d, flags);
    }
    r3d_lamp(&r, 0, 0, 0, 0, 0, 0);
}

static void s_flat(void)      { quads(&quad_m, 0); }
static void s_noz(void)       { quads(&quad_m, R3D_NOZ); }
static void s_gouraud(void)   { quads(&quad_m, R3D_SMOOTH); }
static void s_tex(void)       { quads(&quadtex_m, 0); }
static void s_texunlit(void)  { quads(&quadtex_m, R3D_UNLIT); }
static void s_texsmooth(void) { quads(&quadtex_m, R3D_SMOOTH); }
static void s_texnoz(void)    { quads(&quadtex_m, R3D_NOZ); }

/* the stress test's spheres: many small triangles */
static void spheres(r3d_mesh_t *m, unsigned flags, int n)
{
    target(640, 360);
    g16_cls(&g, g16_rgb(10, 10, 30));
    r3d_zclear(&r);
    r3d_camera(&r, 0, 0, -8, 0, 0, 60);
    r3d_light(&r, -0.4f, 0.7f, -0.6f, 0.25f);
    int side = (int)ceilf(sqrtf((float)n));
    float step = 9.0f / (side > 1 ? side : 1);
    for (int i = 0; i < n; i++) {
        float x = -4.5f + step * (i % side + 0.5f), y = 2.6f - step * 0.56f * (i / side + 0.5f);
        r3d_draw_flags(&r, m, (v3_t){ x, y, (float)(i % 3) }, 0.3f + i, 0.5f, 0, step * 0.45f, flags);
    }
}

static void s_spheres(void)       { spheres(&sphere_m, 0, 30); }
static void s_spheres_smooth(void) { spheres(&sphere_m, R3D_SMOOTH, 30); }
static void s_spheres_tex(void)   { spheres(&spheretex_m, 0, 30); }
static void s_spheres_fog(void)
{
    r3d_fog(&r, 0x304050, 6, 12);
    spheres(&sphere_m, 0, 30);
    spheres(&sphere_m, R3D_SMOOTH, 12);
    r3d_fog(&r, 0, 0, 0);
}

/* Texture Room at 320x180, 8 crates, the camera where the tour starts
 * and a second view across the room */
static void room(float t, int ncrates)
{
    target(320, 180);
    g16_cls(&g, g16_rgb(16, 16, 24));
    r3d_zclear(&r);
    float yaw = t * 0.25f, cx = -sinf(yaw) * 8.5f, cz = -cosf(yaw) * 8.5f;
    r3d_camera(&r, cx, 1.7f, cz, yaw, -0.12f, 70);
    r3d_light(&r, 0.4f, 0.8f, -0.3f, 0.45f);
    r3d_draw_flags(&r, &floor_m, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1, R3D_NOZ);
    r3d_draw_flags(&r, &walls_m, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1, 0);
    static const float pil[4][2] = { { -6, -6 }, { 6, -6 }, { -6, 6 }, { 6, 6 } };
    for (int k = 0; k < 4; k++)
        r3d_draw_flags(&r, &pillar_m, (v3_t){ pil[k][0], 0, pil[k][1] }, 0, 0, 0, 1, 0);
    for (int k = 1; k <= ncrates; k++) {
        float a = (float)k / ncrates * 6.2832f, rad = 4 + (k % 3) * 2.2f;
        float size = 0.55f + (k % 3) * 0.15f, spin = (k % 2 == 0) ? 0.6f : -0.4f;
        float y = size + 0.15f * sinf(t * 2 + k * 0.7f);
        r3d_draw_flags(&r, &crate_m, (v3_t){ cosf(a) * rad, y, sinf(a) * rad }, 0, t * spin, 0, size, 0);
    }
}

static void s_room(void)   { room(0.0f, 8); }
static void s_room2(void)  { room(7.0f, 32); }

/* a textured floor passing under the camera: near clipping */
static void s_clip(void)
{
    target(640, 360);
    g16_cls(&g, 0);
    r3d_zclear(&r);
    r3d_camera(&r, 0, 1, -2, 0.3f, -0.3f, 60);
    r3d_light(&r, 0, 1, 0, 0.5f);
    r3d_draw_flags(&r, &quadtex_m, (v3_t){ 0, 0, 3 }, 1.5707963f, 0, 0, 8, 0);
    r3d_draw_flags(&r, &quad_m, (v3_t){ 2, 0.5f, 3 }, 1.2f, 0.3f, 0, 3, R3D_SMOOTH);
}

typedef struct { const char *name; void (*fn)(void); } scene_t;
static const scene_t scenes[] = {
    { "flat", s_flat }, { "noz", s_noz }, { "gouraud", s_gouraud },
    { "tex", s_tex }, { "texunlit", s_texunlit }, { "texsmooth", s_texsmooth }, { "texnoz", s_texnoz },
    { "spheres", s_spheres }, { "spheres_smooth", s_spheres_smooth }, { "spheres_tex", s_spheres_tex },
    { "spheres_fog", s_spheres_fog }, { "room", s_room }, { "room2", s_room2 }, { "clip", s_clip },
};

static uint32_t checksum(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < g.w * g.h; i++) {
        h = (h ^ (fb[i] & 0xFF)) * 16777619u;
        h = (h ^ (fb[i] >> 8)) * 16777619u;
    }
    return h;
}

static void write_ppm(const char *dir, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 %d %d 255\n", g.w, g.h);
    for (int i = 0; i < g.w * g.h; i++) {
        uint32_t c = g16_to_rgb24(fb[i]);
        fputc((int)(c >> 16), f); fputc((int)(c >> 8 & 255), f); fputc((int)(c & 255), f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *ppm = NULL, *only = NULL;
    int frames = 1, gpu = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!only) only = argv[i];
        else frames = atoi(argv[i]);
    }
    char name[32];
    if (only && strlen(only) > 4 && !strcmp(only + strlen(only) - 4, "+gpu")) {
#ifdef BENCH_GPU
        snprintf(name, sizeof name, "%.*s", (int)(strlen(only) - 4), only);
        only = name;
        gpu = 1;
        fb = test_aligned_alloc(64, sizeof fb_ram);
        if (gpu3d_init() != 0) {
            fprintf(stderr, "gpu3d: %s\n", gpu3d_status());
            return 1;
        }
        /* count_insns traces the ARM only: the emulator's work after the
         * probe (the picture) is not needed there */
        emu_skip = getenv("BENCH_EMU_SKIP") != NULL;
#else
        fprintf(stderr, "built without BENCH_GPU\n");
        return 1;
#endif
    }
    g16_target(&g, fb, 640, 640, 360, &font);
    r3d_init(&r, &g);
#ifdef BENCH_GPU
    if (gpu)
        r.backend = gpu3d_backend();
#endif
    make_sheets();
    make_room();
    make_quads();
    int found = 0;
    for (size_t i = 0; i < sizeof scenes / sizeof *scenes; i++) {
        if (only && strcmp(only, scenes[i].name))
            continue;
        found = 1;
        for (int f = 0; f < frames; f++) {
            scenes[i].fn();
#ifdef BENCH_GPU
            if (gpu && gpu3d_flush(&g, 0) != 0) {
                fprintf(stderr, "gpu3d: %s (%s)\n", gpu3d_status(), emu_error);
                return 1;
            }
#endif
        }
        printf("%-15s %08x %7u px %5u tri\n", scenes[i].name, checksum(), r.pixels, r.tris_drawn);
        if (ppm)
            write_ppm(ppm, scenes[i].name);
    }
    if (!found) {
        fprintf(stderr, "no scene %s\n", only);
        return 1;
    }
    return 0;
}
