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
 * And meshes with a skeleton (heroes), by the box of their pose:
 *   - far, at rest: the same choice as the mesh without bones;
 *   - a crowd of posed heroes: off the picture of before, on the same
 *     choice on the ARM and on the GPU, fewer triangles, a small difference;
 *   - one hero alone, many poses and distances: an impostor only if the
 *     whole mesh drawn is at most that many pixels (an arm thrown away
 *     counts), never a near one.
 * The number is in pixels of a 1080-line picture: N * h / 1080 of the
 * target's (at 640x360 here, AT360(px) asks for px of the target's). And:
 *   - 640x360, 1280x720, 1920x1080: the same N, the same choice (the same
 *     size on the screen), and at 1080 lines the number is the target's.
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

static int W = 640, H = 360;    /* the target: 640x360 but in sizes() */

static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };
static uint16_t *fb;
static g16_t g;
static r3d_t r;
static g16_sheet_t sheet;
static r3d_mesh_t sphere, lamp, box, baked;
static int failed;

/* r.impostor for px pixels of a 360-line target: 3x (1080 / 360) */
#define AT360(px) ((px) * 3)

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failed = 1; } } while (0)

/* the pictures of the scene with the impostors off, as before them (the
 * r3d.c without impostors drew these): the ARM's, the GPU's (with or
 * without its vertex shader) */
#define OFF_ARM_SUM 0x9ac5c23bu
#define OFF_GPU_SUM 0x459ca0feu
/* and of the crowd of heroes (r3d.c before impostors for meshes with a
 * skeleton drew these) */
#define OFF_CROWD_ARM_SUM 0x12a000fdu
#define OFF_CROWD_GPU_SUM 0xd0191095u

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

/* ---- meshes with a skeleton */

#define HERO_V 400
static r3d_mesh_t hero, hero_plain;
static uint8_t hero_vbone[HERO_V];
static float hero_bones[3][12];

/* a figure 2 units tall standing on y = 0: a body (bone 0) and two arms
 * (bones 1 and 2: the vertices left and right of |x| = 0.15); `bones` 0
 * the same mesh without a skeleton */
static void make_hero(r3d_mesh_t *m, int bones)
{
    r3d_mesh_sphere(m, 10, 16, 0xE0A040, 0x4060E0);
    for (int i = 0; i < m->nverts; i++) {
        const v3_t v = m->verts[i];
        m->verts[i] = (v3_t){ v.x * 0.45f, v.y + 1.0f, v.z * 0.3f };
    }
    r3d_mesh_normals(m);
    if (!bones)
        return;
    for (int i = 0; i < m->nverts && i < HERO_V; i++)
        hero_vbone[i] = m->verts[i].x < -0.15f ? 1 : m->verts[i].x > 0.15f ? 2 : 0;
    m->bones = (const float (*)[12])hero_bones;
    m->vbone = hero_vbone;
    m->nbones = 3;
}

/* the pose: the arms turned up by a (radians) around the shoulders, the
 * left one `out` units further left (thrown away) */
static void hero_pose(float a, float out)
{
    static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    memcpy(hero_bones[0], id, sizeof id);
    for (int b = 1; b <= 2; b++) {
        const float t = b == 1 ? -a : a, c = cosf(t), s = sinf(t), px = b == 1 ? -0.15f : 0.15f, py = 1.6f;
        float *M = hero_bones[b];
        /* about z around (px, py, 0): p' = R (p - q) + q */
        M[0] = c; M[1] = -s; M[2] = 0; M[3] = px - (c * px - s * py);
        M[4] = s; M[5] = c;  M[6] = 0; M[7] = py - (s * px + c * py);
        M[8] = 0; M[9] = 0;  M[10] = 1; M[11] = 0;
    }
    hero_bones[1][3] -= out;
}

/* rows of posed heroes going away from the camera (a quarter with an arm
 * thrown away), no fog: the far ones a few pixels */
static void crowd(void)
{
    g16_cls(&g, g16_rgb(10, 10, 30));
    r3d_zclear(&r);
    r3d_camera(&r, 0, 1.5f, -6, 0, 0.05f, 60);
    r3d_light(&r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_shine(&r, 0.6f, 16, 0.3f);
    for (int row = 0; row < 16; row++) {
        const float z = 2 + row * row * 1.2f;
        for (int i = 0; i < 9; i++) {
            const float x = (i - 4) * (1.5f + z * 0.08f);
            hero_pose(0.25f * (float)((row + i) % 5), (row + i) % 4 == 0 ? 2.5f : 0);
            r3d_draw_flags(&r, &hero, (v3_t){ x, 0, z }, 0, 0.4f * i, 0, 1, (i & 1) ? R3D_SMOOTH : 0);
        }
    }
}

typedef struct { uint32_t sum, tris, imps; uint16_t *px; } shot_t;

static void (*draw_scene)(void) = scene;

static shot_t draw(int gpu, int vs, int px)
{
    r.backend = gpu ? gpu3d_backend() : NULL;
    if (gpu)
        gpu3d_set_vshader(vs);
    r.impostor = px;
    draw_scene();
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

static void report(const char *what, const shot_t *off, const shot_t *on, int max_far, float max_mean,
                   uint32_t min_imps)
{
    int far;
    float mean;
    compare(off, on, &far, &mean);
    printf("%-22s off %08x %5u tri | on %08x %5u tri, %u impostors | %5d px differ, mean %.3f\n", what, off->sum,
           off->tris, on->sum, on->tris, on->imps, far, mean);
    CHECK(off->imps == 0, "%s: impostors with the option off", what);
    CHECK(on->imps >= min_imps, "%s: only %u impostors", what, on->imps);
    CHECK(on->tris + 8u * on->imps < off->tris, "%s: not fewer triangles (%u, %u)", what, on->tris, off->tris);
    CHECK(far <= max_far, "%s: %d pixels differ (at most %d)", what, far, max_far);
    CHECK(mean <= max_mean, "%s: mean difference %.3f (at most %.3f)", what, mean, max_mean);
}

/* under 1% of the picture's pixels (an impostor is at most 8x8), the mean
 * far below one step of a colour */
#define MAX_FAR 2000
#define MAX_MEAN 0.25f

/* a scene off and on (8 px), by the ARM and by the GPU (without and with
 * its vertex shader): off the pictures of before, on the same choice
 * everywhere and a small difference. Returns the ARM's count of impostors
 * at 8 px; *arm_off_out, *arm_on_out its two shots */
static uint32_t both(const char *name, void (*fn)(void), uint32_t off_arm, uint32_t off_gpu, uint32_t min_imps,
                     shot_t *arm_off_out, shot_t *arm_on_out)
{
    draw_scene = fn;
    const shot_t arm_off = draw(0, 0, 0), arm_on = draw(0, 0, AT360(8)), arm_on2 = draw(0, 0, AT360(8));
    char what[48];
    snprintf(what, sizeof what, "%s ARM", name);
    CHECK(arm_off.sum == off_arm, "%s, off: %08x, not the picture of before (%08x)", what, arm_off.sum, off_arm);
    CHECK(arm_on.sum == arm_on2.sum, "%s, on: two draws differ", what);
    report(what, &arm_off, &arm_on, MAX_FAR, MAX_MEAN, min_imps);
    free(arm_on2.px);
    for (int vs = 0; vs <= 2; vs += 2) {
        const shot_t off = draw(1, vs, 0), on = draw(1, vs, AT360(8));
        snprintf(what, sizeof what, "%s GPU%s", name, vs ? "+VS" : "");
        CHECK(off.sum == off_gpu, "%s, off: %08x, not the picture of before (%08x)", what, off.sum, off_gpu);
        report(what, &off, &on, MAX_FAR, MAX_MEAN, min_imps);
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
        CHECK(far <= far0 + MAX_FAR / 4, "%s: the impostors differ from the ARM's (%d px, %d without)", what, far,
              far0);
        free(off.px);
        free(on.px);
    }
    *arm_off_out = arm_off;
    *arm_on_out = arm_on;
    return arm_on.imps;
}

/* the box of the pixels that are not black: 0 if none */
static int drawn_box(int *w, int *h)
{
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (fb[y * W + x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    *w = x1 - x0 + 1;
    *h = y1 - y0 + 1;
    return x1 >= 0;
}

/* one hero alone on black (unlit, no fog) by the ARM: impostors drawn,
 * triangles drawn, and the box of its pixels; 0 if no pixel */
static int alone(r3d_mesh_t *m, v3_t p, float yaw, int px, uint32_t *imps, uint32_t *tris, int *w, int *h)
{
    r.backend = NULL;
    r.impostor = px;
    g16_cls(&g, 0);
    r3d_zclear(&r);
    r3d_camera(&r, 0, 1.5f, -6, 0, 0.05f, 60);
    r3d_draw_flags(&r, m, p, 0, yaw, 0, 1, R3D_UNLIT);
    *imps = r.impostors;
    *tris = r.tris_drawn;
    return drawn_box(w, h);
}

/* meshes with a skeleton */
static void skinned(void)
{
    uint32_t ip, tp, is, ts;
    int w, h, ws, hs;
    /* far, at rest: the same choice as without bones */
    hero_pose(0, 0);
    const v3_t far = { 1, 0, 180 };
    alone(&hero_plain, far, 0.3f, AT360(8), &ip, &tp, &w, &h);
    alone(&hero, far, 0.3f, AT360(8), &is, &ts, &ws, &hs);
    printf("far hero at rest (%dx%d px): without bones %u impostor, %u tri; with bones %u impostor, %u tri\n", w, h,
           ip, tp, is, ts);
    CHECK(ip == 1 && tp == 2, "a far hero without bones: %u impostors, %u tri", ip, tp);
    CHECK(is == ip && ts == tp, "a far hero with bones at rest: %u impostors, %u tri (without bones %u, %u)", is, ts,
          ip, tp);
    /* the same place, an arm thrown 3 units away: the pose is wider than
     * 8 px, so not an impostor */
    hero_pose(0, 3);
    alone(&hero, far, 0, 0, &is, &ts, &ws, &hs);
    alone(&hero, far, 0, AT360(8), &is, &ts, &w, &h);
    printf("far hero, an arm thrown away (%dx%d px): %u impostor, %u tri\n", ws, hs, is, ts);
    CHECK(ws > 9 && is == 0, "a far hero %d px wide in its pose: %u impostors", ws, is);

    /* many poses and places: an impostor only when the whole posed mesh
     * drawn is at most 8 px (a pixel more for the edges), never near */
    int chosen = 0, small = 0;
    for (int k = 0; k < 64; k++) {
        const float z = 2 + (float)(k * k) * 0.07f, a = 0.35f * (float)(k % 7), out = (k % 3) ? 0 : 0.6f * (float)(k % 5);
        const v3_t p = { 0.3f * (float)(k % 9) - 1.2f, 0, z };
        const float yaw = 0.37f * (float)k;
        hero_pose(a, out);
        alone(&hero, p, yaw, 0, &is, &ts, &w, &h);
        uint32_t i8, t8;
        alone(&hero, p, yaw, AT360(8), &i8, &t8, &ws, &hs);
        small += w <= 6 && h <= 6;
        if (i8) {
            chosen++;
            CHECK(w <= 9 && h <= 9, "pose %d at z %.1f: an impostor, but the mesh is %dx%d px", k, z, w, h);
        }
        if (w > 16 || h > 16)
            CHECK(!i8, "pose %d at z %.1f: %dx%d px, an impostor", k, z, w, h);
    }
    printf("posed heroes alone: %d impostors of 64 (%d at most 6x6 px)\n", chosen, small);
    CHECK(chosen >= small && chosen >= 8, "posed heroes: %d impostors, %d at most 6x6 px", chosen, small);

    /* out of view on the right with an arm thrown into it: skipped only by
     * the sphere of the pose, so the arm is drawn, the picture of the
     * impostors off; without the arm, skipped */
    const v3_t side = { 14, 0, 14 };
    hero_pose(0, 3);
    const int seen0 = alone(&hero, side, 0, 0, &is, &ts, &w, &h);
    const uint32_t sum0 = checksum(fb);
    uint32_t i8, t8;
    const int seen8 = alone(&hero, side, 0, AT360(8), &i8, &t8, &w, &h);
    printf("out of view, an arm in it: off %s, on %s, %u impostors, %u tri\n", seen0 ? "drawn" : "nothing",
           seen8 ? "drawn" : "nothing", i8, t8);
    CHECK(seen0 && seen8 && checksum(fb) == sum0 && i8 == 0, "an arm reaching into the view: not as without impostors");
    hero_pose(0, 0);
    alone(&hero, side, 0, 0, &is, &ts, &w, &h);
    const int seen = alone(&hero, side, 0, AT360(8), &i8, &t8, &w, &h);
    printf("out of view: off %u tri, on %u tri\n", ts, t8);
    CHECK(!seen && t8 == 0 && i8 == 0, "a hero out of view: %u tri, %u impostors", t8, i8);
}

/* the crowd at more pixels, 16 32 64 of a 1080-line picture (5.3 10.7 21.3
 * of this 360-line one; at 8 none of its heroes is that small): more
 * impostors */
static void crowd_steps(void)
{
    draw_scene = crowd;
    uint32_t last = 0;
    for (int px = 16; px <= 64; px *= 2) {
        const shot_t s = draw(0, 0, px);
        printf("crowd ARM, %2d px (1080p): %3u impostors, %5u tri\n", px, s.imps, s.tris);
        CHECK(s.imps > last, "crowd: %d px gives %u impostors, fewer px %u", px, s.imps, last);
        last = s.imps;
        free(s.px);
    }
}

/* the target w x h (fb holds 1920x1080), a z-buffer of its size */
static void target(int w, int h)
{
    r3d_free(&r);
    W = w;
    H = h;
    g16_target(&g, fb, (uint32_t)W, W, H, &font);
    r3d_init(&r, &g);
}

/* the same scenes at 640x360, 1280x720 and 1920x1080 by the ARM: the same N
 * gives the same choice (the number is pixels of a 1080-line picture, the
 * models the same size on the screen); at 360 lines 8 is 2.7 of the
 * target's pixels, fewer impostors than 8 of them (AT360(8), before) */
static void sizes(void)
{
    static const int lines[3] = { 360, 720, 1080 };
    static const int ns[3] = { 16, 24, 48 };     /* 16: a few models, 48: many */
    static void (*const fns[2])(void) = { scene, crowd };
    static const char *const names[2] = { "field", "crowd" };
    uint32_t imps[2][3][3];
    for (int s = 0; s < 3; s++) {
        target(lines[s] * 16 / 9, lines[s]);
        for (int f = 0; f < 2; f++) {
            draw_scene = fns[f];
            for (int k = 0; k < 3; k++) {
                const shot_t sh = draw(0, 0, ns[k]);
                imps[f][k][s] = sh.imps;
                free(sh.px);
            }
        }
    }
    for (int f = 0; f < 2; f++)
        for (int k = 0; k < 3; k++) {
            printf("%s ARM, %2d px (1080p): %3u impostors at 360 lines, %3u at 720, %3u at 1080\n", names[f], ns[k],
                   imps[f][k][0], imps[f][k][1], imps[f][k][2]);
            CHECK(imps[f][k][0] > 0 && imps[f][k][0] == imps[f][k][1] && imps[f][k][1] == imps[f][k][2],
                  "%s, %d px: not the same choice at 360, 720 and 1080 lines", names[f], ns[k]);
        }
    /* at 1080 lines the number is the target's pixels: a hero alone is an
     * impostor only if it is at most that many (a pixel more for the edges) */
    int chosen = 0;
    for (int k = 0; k < 48; k++) {
        const float z = 150 + (float)(k * k) * 0.27f;
        uint32_t i0, t0, i8, t8;
        int w, h, w8, h8;
        hero_pose(0.35f * (float)(k % 7), 0);
        alone(&hero, (v3_t){ 0.3f * (float)(k % 9) - 1.2f, 0, z }, 0.37f * (float)k, 0, &i0, &t0, &w, &h);
        alone(&hero, (v3_t){ 0.3f * (float)(k % 9) - 1.2f, 0, z }, 0.37f * (float)k, 8, &i8, &t8, &w8, &h8);
        if (i8) {
            chosen++;
            CHECK(w <= 9 && h <= 9, "1080 lines, hero %d at z %.1f: an impostor, but %dx%d px", k, z, w, h);
        }
        if (w > 16 || h > 16)
            CHECK(!i8, "1080 lines, hero %d at z %.1f: %dx%d px, an impostor", k, z, w, h);
    }
    printf("heroes alone at 1080 lines, 8 px: %d impostors of 48\n", chosen);
    CHECK(chosen >= 4, "1080 lines: only %d heroes alone are impostors", chosen);
    /* 360 lines: 8 (1080p) is a third of 8 of the target's */
    target(640, 360);
    draw_scene = scene;
    const shot_t a = draw(0, 0, 8), b = draw(0, 0, AT360(8));
    printf("field ARM at 360 lines: 8 px (1080p) %u impostors, 24 px (1080p, 8 here) %u\n", a.imps, b.imps);
    CHECK(a.imps < b.imps, "360 lines: 8 px gives %u impostors, 24 px %u", a.imps, b.imps);
    free(a.px);
    free(b.px);
}

int main(void)
{
    fb = test_aligned_alloc(64, 1920 * 1080 * 2);
    g16_target(&g, fb, W, W, H, &font);
    r3d_init(&r, &g);
    make_meshes();
    make_hero(&hero, 1);
    make_hero(&hero_plain, 0);
    if (gpu3d_init() != 0) {
        printf("FAIL: gpu3d: %s\n", gpu3d_status());
        return 1;
    }

    shot_t arm_off, arm_on;
    both("field", scene, OFF_ARM_SUM, OFF_GPU_SUM, 40, &arm_off, &arm_on);
    const shot_t arm_on6 = draw(0, 0, AT360(6));
    CHECK(arm_on6.imps < arm_on.imps && arm_on6.imps > 0, "ARM: 6 px gives %u impostors, 8 px %u", arm_on6.imps,
          arm_on.imps);
    printf("field ARM, 6 px: %u impostors, %u tri\n", arm_on6.imps, arm_on6.tris);
    free(arm_off.px);
    free(arm_on.px);
    free(arm_on6.px);

    skinned();
    both("crowd", crowd, OFF_CROWD_ARM_SUM, OFF_CROWD_GPU_SUM, 20, &arm_off, &arm_on);
    free(arm_off.px);
    free(arm_on.px);
    crowd_steps();
    sizes();
    if (failed)
        return 1;
    printf("impostors: ok\n");
    return 0;
}
