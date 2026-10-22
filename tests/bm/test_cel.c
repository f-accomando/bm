/*
 * Cel light on the ARM raster (cel3d, draw3d flag 256, dither3d; bm3D 7.4).
 *   - the table: N bands, the floor, the edges of the bands (exact for 2, 4, 8 bands);
 *   - a lit sphere with no ambient light: a flat cel draw shows exactly N grey levels, the
 *     darkest is the floor; a smooth cel draw shows more (the blend) and differs from the flat one;
 *   - off: bands 0 with the flag, bands N without the flag, and unlit with the flag give the
 *     picture of a draw with no cel (OFF_*_SUM are those pictures made by the code before cel);
 *   - dither3d(false) changes a smooth picture, dither3d(true) brings the first one back.
 * Built with -DOLD_BUILD against the r3d.c before cel, it only prints the checksums of the
 * plain scenes (to make OFF_*_SUM).
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/r3d.h"

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

#define W 320
#define H 240
static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };
static uint16_t *fb;
static g16_t g;
static r3d_t r;
static r3d_mesh_t sphere, box;
static g16_sheet_t sheet;
static int failed;
static const uint16_t BG = 0xF81F;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failed = 1; } } while (0)

/* the pictures made by the code before cel (build with -DOLD_BUILD to print them) */
#define OFF_SPHERE_SUM 0xa48d9befu
#define OFF_SMOOTH_SUM 0xc67e1d12u

static uint32_t checksum(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * H; i++) {
        h = (h ^ (fb[i] & 0xFF)) * 16777619u;
        h = (h ^ (fb[i] >> 8)) * 16777619u;
    }
    return h;
}

/* one mesh alone, white sun from the camera's side */
static void draw(r3d_mesh_t *m, unsigned flags, float ambient)
{
    g16_cls(&g, BG);
    r3d_zclear(&r);
    r3d_camera(&r, 0, 0, -5, 0, 0, 40);
    r3d_light(&r, 0.3f, 0.4f, -1, ambient);
    r3d_shine(&r, 0.6f, 16, 0);
    r3d_draw_flags(&r, m, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1.5f, flags);
}

/* the distinct pixel values on the mesh (not the background); *lo = the darkest green */
static int levels(int *lo)
{
    static uint8_t seen[65536];
    memset(seen, 0, sizeof seen);
    int n = 0, dark = 255;
    for (int i = 0; i < W * H; i++) {
        const uint16_t p = fb[i];
        if (p == BG || seen[p]) continue;
        seen[p] = 1;
        n++;
        const int gg = (p >> 5 & 63) << 2;
        if (gg < dark) dark = gg;
    }
    if (lo) *lo = dark;
    return n;
}

#ifndef OLD_BUILD
static float expect(int bands, float floor_, int band)
{
    return floor_ + (1.0f - floor_) * (float)band / (float)(bands - 1);
}

static void table(void)
{
    for (int bands = 2; bands <= 8; bands++) {
        for (int fi = 0; fi <= 4; fi++) {
            const float fl = fi * 0.25f;
            CHECK(r3d_cel(&r, bands, fl) == bands, "r3d_cel returns the bands");
            CHECK(fabsf(r3d_cel_cut(&r, 0) - fl) < 0.004f, "N=%d floor %.2f: dd 0 gives %.3f", bands, fl,
                  r3d_cel_cut(&r, 0));
            CHECK(fabsf(r3d_cel_cut(&r, 1) - 1.0f) < 0.004f, "N=%d: dd 1 gives %.3f", bands, r3d_cel_cut(&r, 1));
            /* the distinct values over dd: N of them (one when the floor is 1) */
            float vals[16];
            int nv = 0;
            for (int i = 0; i <= 1000; i++) {
                const float v = r3d_cel_cut(&r, i / 1000.0f);
                int k;
                for (k = 0; k < nv; k++)
                    if (fabsf(vals[k] - v) < 1e-6f) break;
                if (k == nv && nv < 16) vals[nv++] = v;
            }
            const int want = fl >= 1.0f ? 1 : bands;
            CHECK(nv == want, "N=%d floor %.2f: %d levels, want %d", bands, fl, nv, want);
            /* the edges: exact when N divides 64 */
            if (64 % bands == 0) {
                for (int b = 1; b < bands; b++) {
                    const float edge = (float)b / bands;
                    CHECK(fabsf(r3d_cel_cut(&r, edge - 0.002f) - expect(bands, fl, b - 1)) < 0.004f,
                          "N=%d edge %d: below", bands, b);
                    CHECK(fabsf(r3d_cel_cut(&r, edge + 0.002f) - expect(bands, fl, b)) < 0.004f,
                          "N=%d edge %d: above", bands, b);
                }
            }
        }
    }
    CHECK(r3d_cel(&r, 0, 0.35f) == 0 && r.cel_bands == 0, "bands 0 is off");
    CHECK(r3d_cel(&r, 1, 0.35f) == 0, "bands 1 is off");
    CHECK(r3d_cel(&r, 99, 0.35f) == 8, "bands are limited to 8");
    printf("table: ok\n");
}
#endif

int main(void)
{
    fb = malloc((size_t)W * H * 2 + 64);
    g16_target(&g, fb, W, W, H, &font);
    r3d_init(&r, &g);
    r3d_mesh_sphere(&sphere, 24, 48, 0xFFFFFF, 0xFFFFFF);
    r3d_sky(&r, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
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
        static const float uv[2][6] = { { 0, 64, 0, 0, 64, 0 }, { 0, 64, 64, 0, 64, 64 } };
        box.colors[t] = R3D_TEXTURED;
        memcpy(box.uv + t * 6, uv[t & 1], sizeof uv[0]);
    }
    box.tex = &sheet;
    r3d_mesh_normals(&box);

    draw(&sphere, 0, 0.0f);
    const uint32_t plain = checksum();
    const int plain_levels = levels(NULL);
    draw(&sphere, R3D_SMOOTH, 0.0f);
    const uint32_t plain_smooth = checksum();
#ifdef OLD_BUILD
    printf("#define OFF_SPHERE_SUM 0x%08xu\n#define OFF_SMOOTH_SUM 0x%08xu\n", plain, plain_smooth);
    (void)plain_levels;
    return 0;
#else
    CHECK(plain == OFF_SPHERE_SUM, "no cel: the sphere is 0x%08x, before cel it was 0x%08x", plain, OFF_SPHERE_SUM);
    CHECK(plain_smooth == OFF_SMOOTH_SUM, "no cel: the smooth sphere is 0x%08x, before 0x%08x", plain_smooth,
          OFF_SMOOTH_SUM);
    CHECK(plain_levels > 12, "plain sphere has %d levels", plain_levels);
    table();

    /* off */
    r3d_cel(&r, 0, 0.35f);
    draw(&sphere, R3D_CEL, 0.0f);
    CHECK(checksum() == plain, "bands 0 with the flag changes the picture");
    r3d_cel(&r, 4, 0.35f);
    draw(&sphere, 0, 0.0f);
    CHECK(checksum() == plain, "bands 4 without the flag changes the picture");
    draw(&sphere, R3D_UNLIT | R3D_CEL, 0.0f);
    const uint32_t unlit_cel = checksum();
    draw(&sphere, R3D_UNLIT, 0.0f);
    CHECK(unlit_cel == checksum(), "unlit with the flag changes the picture");
    draw(&sphere, R3D_OUTLINE, 0.0f);
    CHECK(checksum() == plain, "the outline flag (reserved) changes the picture");

    /* bands and floor on the sphere */
    for (int bands = 2; bands <= 8; bands++) {
        for (int fi = 1; fi <= 3; fi++) {
            const float fl = fi * 0.25f;
            r3d_cel(&r, bands, fl);
            draw(&sphere, R3D_CEL, 0.0f);
            int lo;
            const int n = levels(&lo);
            CHECK(n == bands, "N=%d floor %.2f: %d grey levels on the sphere", bands, fl, n);
            CHECK(abs(lo - (int)(fl * 255)) <= 6, "N=%d: darkest green %d, floor %d", bands, lo, (int)(fl * 255));
        }
    }
    /* smooth: more levels than bands (the blend), another picture than the flat one */
    r3d_cel(&r, 4, 0.35f);
    draw(&sphere, R3D_CEL, 0.0f);
    const uint32_t flat_cel = checksum();
    draw(&sphere, R3D_CEL | R3D_SMOOTH, 0.0f);
    const int smooth_levels = levels(NULL);
    CHECK(checksum() != flat_cel && smooth_levels > 4, "smooth cel: %d levels", smooth_levels);
    /* an ambient term is kept (the sky and ground are white: it adds a constant) */
    draw(&sphere, R3D_CEL, 0.5f);
    CHECK(levels(NULL) == 4, "ambient 0.5: %d levels", levels(NULL));
    /* a textured face is cut too */
    draw(&box, 0, 0.0f);
    const uint32_t box_sum = checksum();
    const int box_plain = levels(NULL);
    draw(&box, R3D_CEL, 0.0f);
    const int box_cel = levels(NULL);
    CHECK(checksum() != box_sum, "textured box: the cel light changes nothing");
    printf("cel: ok (plain %d levels, smooth cel %d, box %d -> %d)\n", plain_levels, smooth_levels, box_plain,
           box_cel);

    /* dither3d */
    r3d_cel(&r, 0, 0.35f);
    CHECK(r3d_dither(0) == 1, "dither starts on");
    draw(&sphere, R3D_SMOOTH, 0.0f);
    CHECK(checksum() != plain_smooth, "dither off does not change the picture");
    CHECK(r3d_dither(1) == 0, "dither3d returns the old state");
    draw(&sphere, R3D_SMOOTH, 0.0f);
    CHECK(checksum() == plain_smooth, "dither back on gives the first picture");
    printf("dither: ok\n");
    if (failed)
        return 1;
    printf("cel: all ok\n");
    return 0;
#endif
}
