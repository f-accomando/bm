/* Host tests for the .bm parser and the RGB565 drawing library. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm/bm.h"
#include "bm/gfx16.h"
#include "bm/r3d.h"
#include "lib/crc32.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t glyphs[256 * 16];
static const font_t font = { 8, 16, glyphs };

#define W 64
#define H 48
static uint16_t fb[H * W];

static uint16_t at(int x, int y) { return fb[y * W + x]; }

static void test_primitives(void)
{
    g16_t g;
    g16_target(&g, fb, W, W, H, &font);
    g16_cls(&g, 0x1234);
    CHECK(at(0, 0) == 0x1234 && at(W - 1, H - 1) == 0x1234, "cls");

    g16_rectfill(&g, 10, 10, 5, 3, 0xF800);
    CHECK(at(10, 10) == 0xF800 && at(14, 12) == 0xF800, "rectfill inside");
    CHECK(at(15, 10) == 0x1234 && at(10, 13) == 0x1234, "rectfill size is w x h");

    g16_clip(&g, 20, 20, 4, 4);
    g16_rectfill(&g, 0, 0, W, H, 0x07E0);
    CHECK(at(20, 20) == 0x07E0 && at(23, 23) == 0x07E0, "clip keeps inside");
    CHECK(at(19, 20) == 0x1234 && at(24, 23) == 0x1234, "clip cuts outside");
    g16_clip(&g, 0, 0, 0, 0);

    g16_camera(&g, 5, 5);
    g16_pset(&g, 5, 5, 0x001F);
    CHECK(at(0, 0) == 0x001F, "camera offsets pset");
    CHECK(g16_pget(&g, 5, 5) == 0x001F, "pget with camera");
    CHECK(g16_pget(&g, 0, 0) == -1, "pget outside");
    g16_camera(&g, 0, 0);

    g16_cls(&g, 0);
    g16_line(&g, 0, 0, 7, 7, 0xFFFF);
    int diag = 1;
    for (int i = 0; i < 8; i++) diag &= at(i, i) == 0xFFFF;
    CHECK(diag && at(1, 0) == 0, "diagonal line");
    g16_line(&g, -100, 30, 100, 30, 0xFFFF);
    CHECK(at(0, 30) == 0xFFFF && at(W - 1, 30) == 0xFFFF, "clipped horizontal line");

    g16_cls(&g, 0);
    g16_circfill(&g, 30, 20, 5, 0xAAAA);
    CHECK(at(30, 20) == 0xAAAA && at(35, 20) == 0xAAAA && at(36, 20) == 0 && at(30, 26) == 0, "circfill");
    g16_circ(&g, -3, -3, 2, 0xFFFF);        /* fully off screen: must not crash */

    CHECK(g16_rgb(255, 0, 0) == 0xF800 && g16_rgb(0, 255, 0) == 0x07E0 && g16_rgb(0, 0, 255) == 0x001F, "rgb565");
    CHECK(g16_to_rgb24(0xF800) == 0xFF0000 && g16_to_rgb24(0xFFFF) == 0xFFFFFF, "rgb565 -> rgb24");
}

static void test_sprites(void)
{
    g16_t g;
    g16_sheet_t s;
    g16_target(&g, fb, W, W, H, &font);
    CHECK(g16_sheet_alloc(&s, 32, 16) == 0, "sheet alloc");
    /* cell 1: opaque, value = x + y*8; cell 2: transparent except column 0 */
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            g16_sheet_set(&s, 8 + x, y, (uint16_t)(100 + x + y * 8), 1);
            g16_sheet_set(&s, 16 + x, y, 0xFFFF, x == 0);
        }
    for (int c = 0; c < 8; c++) g16_sheet_update_cell(&s, c % 4, c / 4);
    CHECK(s.cell_opaque[1] == 1 && s.cell_opaque[2] == 0, "cell opacity");

    g16_cls(&g, 0);
    g16_spr(&g, &s, 1, 4, 4, 1, 1, 0, 0);
    CHECK(at(4, 4) == 100 && at(11, 4) == 107 && at(4, 11) == 156, "spr");
    g16_spr(&g, &s, 1, 20, 4, 1, 1, 1, 0);
    CHECK(at(20, 4) == 107 && at(27, 4) == 100, "spr flip x");
    g16_spr(&g, &s, 1, 36, 4, 1, 1, 0, 1);
    CHECK(at(36, 4) == 156 && at(36, 11) == 100, "spr flip y");

    g16_cls(&g, 0x1111);
    g16_spr(&g, &s, 2, 4, 20, 1, 1, 0, 0);
    CHECK(at(4, 20) == 0xFFFF && at(5, 20) == 0x1111, "transparency");
    g16_spr(&g, &s, 2, 20, 20, 1, 1, 1, 0);
    CHECK(at(27, 20) == 0xFFFF && at(20, 20) == 0x1111, "transparent flip");

    g16_cls(&g, 0);
    g16_spr(&g, &s, 1, -3, -2, 1, 1, 0, 0);
    CHECK(at(0, 0) == 100 + 3 + 2 * 8, "sprite clipped at top-left");
    g16_spr(&g, &s, 1, W - 2, H - 1, 1, 1, 0, 0);
    CHECK(at(W - 1, H - 1) == 101, "sprite clipped at bottom-right");

    /* opaque fast path must match the masked path */
    static uint16_t a[H * W];
    g16_cls(&g, 0);
    s.cell_opaque[1] = 0;
    g16_spr(&g, &s, 1, 13, 17, 1, 1, 0, 0);
    memcpy(a, fb, sizeof a);
    s.cell_opaque[1] = 1;
    g16_cls(&g, 0);
    g16_spr(&g, &s, 1, 13, 17, 1, 1, 0, 0);
    CHECK(memcmp(a, fb, sizeof a) == 0, "opaque fast path identical");

    g16_map_t m = { 3, 2, (uint16_t[]){ 1, 0, 2, 0, 1, 1 } };
    g16_cls(&g, 0x2222);
    g16_map(&g, &s, &m, 0, 0, 0, 0, 3, 2);
    CHECK(at(0, 0) == 100 && at(8, 0) == 0x2222 && at(16, 0) == 0xFFFF && at(17, 0) == 0x2222, "map, 0 = empty");
    CHECK(at(8, 8) == 100 && at(0, 8) == 0x2222, "map second row");
    g16_sheet_free(&s);
}

static void test_text(void)
{
    g16_t g;
    g16_target(&g, fb, W, W, H, &font);
    memset(glyphs + 'A' * 16, 0, 16);
    glyphs['A' * 16 + 3] = 0x81;             /* row 3: leftmost and rightmost pixel */
    g16_cls(&g, 0);
    int end = g16_text(&g, 2, 1, "AA", 0xFFFF);
    CHECK(end == 18, "text advance");
    CHECK(at(2, 4) == 0xFFFF && at(9, 4) == 0xFFFF && at(3, 4) == 0 && at(10, 4) == 0xFFFF, "glyph pixels");
}

static void test_light(void)
{
    static uint16_t px[64 * 64];
    g16_t g;
    g16_light_t l;
    g16_target(&g, px, 64, 64, 64, &font);
    CHECK(g16_light_init(&l, 64, 64) == 0, "light init");
    g16_cls(&g, g16_rgb(200, 160, 120));
    uint16_t base = px[0];
    g16_light_clear(&l, 0xFFFFFF);
    g16_light_apply(&g, &l);
    CHECK(px[0] == base && px[33 * 64 + 20] == base, "white light keeps the picture (%04x)", px[0]);
    g16_light_clear(&l, 0x000000);
    g16_light_add(&l, 32, 32, 20, 0xFFFFFF, 1.0f);
    g16_light_apply(&g, &l);
    uint32_t centre = g16_to_rgb24(px[32 * 64 + 32]), edge = g16_to_rgb24(px[32 * 64 + 45]);
    CHECK(px[2 * 64 + 2] == 0, "dark outside the light (%04x)", px[2 * 64 + 2]);
    CHECK((centre >> 16) > 180 && (centre >> 16) > (edge >> 16) && (edge >> 16) > 10,
          "soft light: centre %06x edge %06x", centre, edge);
    /* a warm light over a grey wall comes out orange, and can brighten */
    g16_cls(&g, g16_rgb(100, 100, 100));
    g16_light_clear(&l, 0x000000);
    g16_light_add(&l, 32, 32, 30, 0xFFA040, 2.0f);
    g16_light_apply(&g, &l);
    uint32_t c = g16_to_rgb24(px[32 * 64 + 32]);
    CHECK((c >> 16) > 150 && (c >> 16) > (c >> 8 & 255) && (c >> 8 & 255) > (c & 255), "warm (%06x)", c);
    g16_light_free(&l);
}

static void test_3d(void)
{
    static uint16_t big[360 * 640];
    g16_t g;
    r3d_t r;
    r3d_mesh_t sphere, cube;
    g16_target(&g, big, 640, 640, 360, &font);
    CHECK(r3d_init(&r, &g) == 0, "r3d init");
    CHECK(r3d_mesh_sphere(&sphere, 8, 16, 0xFF0000, 0xFF0000) == 0, "sphere");
    CHECK(r3d_mesh_cube(&cube, 0x00FF00) == 0, "cube");

    int outward = 0;
    for (int t = 0; t < 12; t++) {
        v3_t a = cube.verts[cube.faces[t * 3]], n = cube.normals[t];
        outward += n.x * a.x + n.y * a.y + n.z * a.z > 0;
    }
    CHECK(outward == 12, "cube normals point outwards (%d)", outward);

    /* camera at z=-5 looking +z: a sphere at the origin fills the centre */
    g16_cls(&g, 0);
    r3d_zclear(&r);
    r3d_camera(&r, 0, 0, -5, 0, 0, 60);
    r3d_light(&r, 0, 0, -1, 0);              /* light from the camera, no ambient */
    r3d_draw(&r, &sphere, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1);
    /* from 5 radii away ~40% of a sphere is visible; pole faces are degenerate */
    CHECK(r.tris_drawn > (uint32_t)sphere.nfaces / 4 && r.tris_drawn < (uint32_t)sphere.nfaces / 2,
          "back faces culled (%u of %d drawn)", r.tris_drawn, sphere.nfaces);
    uint32_t centre = g16_to_rgb24(big[180 * 640 + 320]);
    CHECK((centre >> 16) > 200 && (centre & 0xFFFF) == 0, "front of the sphere lit (%06x)", centre);
    CHECK(big[5 * 640 + 5] == 0, "background untouched");

    /* z-buffer: a cube in front hides the sphere, one behind does not */
    r3d_draw(&r, &cube, (v3_t){ 0, 0, 5 }, 0, 0, 0, 0.3f);
    CHECK(g16_to_rgb24(big[180 * 640 + 320]) >> 16 > 200, "cube behind is hidden");
    r3d_draw(&r, &cube, (v3_t){ 0, 0, -2 }, 0, 0, 0, 0.3f);
    CHECK((g16_to_rgb24(big[180 * 640 + 320]) >> 8 & 0xFF) > 200, "cube in front is visible");

    /* behind the camera: nothing drawn, no crash */
    uint32_t before = r.tris_drawn;
    r3d_draw(&r, &sphere, (v3_t){ 0, 0, -20 }, 0, 0, 0, 1);
    CHECK(r.tris_drawn == before, "objects behind the camera are skipped");

    /* near-plane clipping: a floor that passes under the camera is drawn
     * down to the bottom of the screen, not dropped */
    g16_cls(&g, 0);
    r3d_zclear(&r);
    r3d_light(&r, 0, 1, 0, 0);
    r3d_draw(&r, &cube, (v3_t){ 0, -7, 0 }, 0, 0, 0, 6);
    CHECK((g16_to_rgb24(big[358 * 640 + 320]) >> 8 & 0xFF) > 200, "floor clipped at the near plane");
    CHECK(big[5 * 640 + 320] == 0, "sky above the floor");

    /* fog: far faces take the fog colour */
    g16_cls(&g, 0);
    r3d_zclear(&r);
    r3d_fog(&r, 0x0000FF, 1, 2);
    r3d_draw(&r, &cube, (v3_t){ 0, 0, 20 }, 0, 0, 0, 1);
    CHECK(g16_to_rgb24(big[180 * 640 + 320]) == 0x0000FF, "fogged face (%06x)", g16_to_rgb24(big[180 * 640 + 320]));
    r3d_fog(&r, 0, 0, 0);

    /* roll: a point right of centre goes down when the camera rolls +90 deg */
    float sx, sy, d;
    r3d_camera(&r, 0, 0, -5, 0, 0, 60);
    r3d_camera_roll(&r, 1.5707963f);
    CHECK(r3d_project(&r, (v3_t){ 1, 0, 0 }, &sx, &sy, &d) && sx > 319 && sx < 321 && sy > 250 && d > 4.9f,
          "roll (%f, %f)", sx, sy);
    r3d_camera_roll(&r, 0);
    CHECK(!r3d_project(&r, (v3_t){ 0, 0, -6 }, &sx, &sy, &d), "behind the camera");

    /* textured quad facing the camera: left half red texels, right half
     * blue; transparent texels leave the background */
    {
        g16_sheet_t tex;
        r3d_mesh_t q;
        CHECK(g16_sheet_alloc(&tex, 16, 16) == 0, "texture");
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                g16_sheet_set(&tex, x, y, x < 8 ? g16_rgb(255, 0, 0) : g16_rgb(0, 0, 255), y < 12);
        CHECK(r3d_mesh_alloc(&q, 4, 2) == 0 && r3d_mesh_alloc_uv(&q) == 0, "quad");
        q.verts[0] = (v3_t){ -1, -1, 0 }; q.verts[1] = (v3_t){ 1, -1, 0 };
        q.verts[2] = (v3_t){ 1, 1, 0 };   q.verts[3] = (v3_t){ -1, 1, 0 };
        static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
        memcpy(q.faces, f, sizeof f);
        /* texel v grows downwards: vertex y = +1 is v = 0 */
        static const float uv[12] = { 0, 16, 16, 0, 16, 16,   0, 16, 0, 0, 16, 0 };
        memcpy(q.uv, uv, sizeof uv);
        q.colors[0] = q.colors[1] = R3D_TEXTURED;
        q.tex = &tex;
        r3d_mesh_normals(&q);
        g16_cls(&g, 0);
        r3d_zclear(&r);
        r3d_camera(&r, 0, 0, -3, 0, 0, 60);
        r3d_light(&r, 0, 0, -1, 1);          /* full ambient: texels unchanged */
        r3d_draw(&r, &q, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1);
        CHECK(big[150 * 640 + 280] == g16_rgb(255, 0, 0), "left texel red (%04x)", big[150 * 640 + 280]);
        CHECK(big[150 * 640 + 360] == g16_rgb(0, 0, 255), "right texel blue (%04x)", big[150 * 640 + 360]);
        CHECK(big[320 * 640 + 300] == 0, "transparent texels skipped (%04x)", big[320 * 640 + 300]);
        /* the same quad as a floor passing under the camera: clipped, still textured */
        g16_cls(&g, 0);
        r3d_zclear(&r);
        r3d_draw(&r, &q, (v3_t){ 0, -1, 0 }, 1.5707963f, 0, 0, 8);
        uint16_t px = big[350 * 640 + 300];
        CHECK(px == g16_rgb(255, 0, 0) || px == g16_rgb(0, 0, 255), "clipped textured floor (%04x)", px);
        r3d_mesh_free(&q);
        g16_sheet_free(&tex);
    }

    /* 2D triangle */
    g16_cls(&g, 0);
    g16_tri(&g, 10, 10, 50, 10, 10, 50, 0xFFFF);
    CHECK(big[12 * 640 + 12] == 0xFFFF && big[45 * 640 + 45] == 0, "2D triangle");
    g16_tri(&g, -100, -100, 1000, -50, 300, 1000, 0x1234);   /* huge, clipped */
    CHECK(big[180 * 640 + 320] == 0x1234, "clipped big triangle");

    /* 2D Gouraud triangle: near each corner its own colour, in between a blend */
    g16_cls(&g, 0);
    g16_tri_gouraud(&g, 0, 0, 200, 0, 0, 200, 0xFF0000, 0x00FF00, 0x0000FF);
    {
        uint32_t c0 = g16_to_rgb24(big[1 * 640 + 1]), c1 = g16_to_rgb24(big[1 * 640 + 196]);
        uint32_t c2 = g16_to_rgb24(big[196 * 640 + 1]), cm = g16_to_rgb24(big[66 * 640 + 66]);
        CHECK((c0 >> 16) > 0xF0 && (c0 & 0xFFFF) < 0x1010, "gouraud corner red (%06x)", c0);
        CHECK((c1 >> 8 & 0xFF) > 0xF0 && (c1 >> 16) < 0x10, "gouraud corner green (%06x)", c1);
        CHECK((c2 & 0xFF) > 0xF0 && (c2 >> 8) < 0x1010, "gouraud corner blue (%06x)", c2);
        CHECK((cm >> 16) > 0x40 && (cm >> 16) < 0xC0 && (cm >> 8 & 0xFF) > 0x40 && (cm & 0xFF) > 0x40,
              "gouraud centre is a blend (%06x)", cm);
    }

    /* smooth sphere: the light changes inside every face, so neighbouring
     * pixels along a row differ more often than on the flat one; nothing
     * leaks outside the silhouette */
    {
        int changes[2] = { 0, 0 };
        for (int smooth = 0; smooth < 2; smooth++) {
            g16_cls(&g, 0);
            r3d_zclear(&r);
            r3d_camera(&r, 0, 0, -3, 0, 0, 60);
            r3d_light(&r, -0.5f, 0.6f, -0.6f, 0.2f);
            r3d_draw_flags(&r, &sphere, (v3_t){ 0, 0, 0 }, 0.3f, 0.2f, 0, 1, smooth ? R3D_SMOOTH : 0);
            for (int x = 250; x < 390; x++)
                changes[smooth] += big[180 * 640 + x] != big[180 * 640 + x + 1];
            CHECK(big[5 * 640 + 5] == 0, "smooth sphere: background untouched");
        }
        CHECK(changes[1] > changes[0] * 3, "smooth shading varies inside faces (%d vs %d)",
              changes[1], changes[0]);
    }

    r3d_mesh_free(&sphere);
    r3d_mesh_free(&cube);
    r3d_free(&r);
}

static uint8_t *read_file(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void test_format(const char *path)
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    CHECK(d != NULL, "read %s", path);
    if (!d) return;
    bm_cart_t c;
    char err[64] = "";
    CHECK(bm_parse(d, n, &c, err, sizeof err) == 0, "parse demo: %s", err);
    CHECK(strcmp(c.title, "bm native demo") == 0, "title '%s'", c.title);
    CHECK(c.width == 640 && c.height == 360 && c.pixel_format == 1, "video mode");
    CHECK(c.lua && c.lua_size > 100 && memcmp(c.lua, "--", 2) == 0, "lua section");
    CHECK(c.sheet_w == 128 && c.sheet_h == 128 && c.sheet_rgba, "sheet");
    CHECK(c.map_w == 160 && c.map_h == 90 && c.map_cells, "map");

    d[n - 1] ^= 1;
    CHECK(bm_parse(d, n, &c, err, sizeof err) != 0 && strstr(err, "CRC"), "corruption detected");
    d[n - 1] ^= 1;
    memcpy(d, "BMCARXXX", 8);
    CHECK(bm_parse(d, n, &c, err, sizeof err) != 0, "bad magic rejected");
    CHECK(bm_parse(d, 10, &c, err, sizeof err) != 0, "short file rejected");
    free(d);
}

/* A SHEET8 section built by hand: palette, literal and repeated runs. */
static uint8_t px8[4][8][4];

static void set8(void *ctx, int x, int y, const uint8_t rgba[4])
{
    (void)ctx;
    memcpy(px8[y][x], rgba, 4);
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

static void test_sheet8(void)
{
    static const char lua[] = "-- sheet8\n";
    uint8_t sec[64];
    size_t n = 0;
    put16(sec, 8); put16(sec + 2, 4); put16(sec + 4, 3); put16(sec + 6, 0);
    n = 8;
    const uint8_t pal[12] = { 0, 0, 0, 0,  255, 0, 0, 255,  0, 0, 255, 255 };
    memcpy(sec + n, pal, 12); n += 12;
    /* 32 pixels: 20 transparent (repeat), 3 literals 1 2 1, 9 blue (repeat) */
    sec[n++] = 20 + 126; sec[n++] = 0;
    sec[n++] = 2; sec[n++] = 1; sec[n++] = 2; sec[n++] = 1;
    sec[n++] = 9 + 126; sec[n++] = 2;

    uint8_t cart[BM_HEADER_SIZE + 32 + 16 + 64] = { 0 };
    size_t off = BM_HEADER_SIZE + 32;
    memcpy(cart, "BMCART\0\0", 8);
    put16(cart + 8, 1); put16(cart + 10, BM_HEADER_SIZE); put16(cart + 12, 640); put16(cart + 14, 360);
    cart[16] = BM_FMT_RGB565; cart[17] = 2;
    uint8_t *t = cart + BM_HEADER_SIZE;
    put32(t, BM_SEC_LUA); put32(t + 4, (uint32_t)off); put32(t + 8, sizeof lua - 1);
    memcpy(cart + off, lua, sizeof lua - 1);
    size_t off2 = off + 16;
    put32(t + 16, BM_SEC_SHEET8); put32(t + 20, (uint32_t)off2); put32(t + 24, (uint32_t)n);
    memcpy(cart + off2, sec, n);
    size_t len = off2 + n;
    put32(cart + 20, crc32(cart + BM_HEADER_SIZE, (uint32_t)(len - BM_HEADER_SIZE)));

    bm_cart_t c;
    char err[64] = "";
    CHECK(bm_parse(cart, len, &c, err, sizeof err) == 0, "sheet8 parse: %s", err);
    CHECK(c.sheet8 && c.sheet_w == 8 && c.sheet_h == 4 && !c.sheet_rgba, "sheet8 size");
    CHECK(bm_sheet8_unpack(&c, set8, NULL) == 0, "sheet8 unpack");
    CHECK(px8[0][0][3] == 0 && px8[2][3][3] == 0, "transparent run");
    CHECK(px8[2][4][0] == 255 && px8[2][5][2] == 255 && px8[2][6][0] == 255, "literal run");
    CHECK(px8[2][7][2] == 255 && px8[3][7][2] == 255 && px8[3][7][3] == 255, "repeated run");

    /* a run past the end is refused */
    cart[off2 + 20] = 30 + 126;
    put32(cart + 20, crc32(cart + BM_HEADER_SIZE, (uint32_t)(len - BM_HEADER_SIZE)));
    CHECK(bm_parse(cart, len, &c, err, sizeof err) != 0 && strstr(err, "sheet"), "broken runs refused");
}

/* A MESH section built by hand: two models, textured and coloured faces. */
static size_t put_f32(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); put32(p, u); return 4; }

static size_t mesh_model(uint8_t *p, const char *name, int nv, const float *v, int nf, const uint16_t *f,
                         const uint32_t *col, const uint16_t *uv8)
{
    size_t n = 0;
    memset(p, 0, 16);
    memcpy(p, name, strlen(name));
    n = 16;
    put16(p + n, (unsigned)nv); put16(p + n + 2, (unsigned)nf); put32(p + n + 4, 0);
    n += 8;
    for (int i = 0; i < nv * 3; i++) n += put_f32(p + n, v[i]);
    for (int i = 0; i < nf; i++) {
        put16(p + n, f[i * 3]); put16(p + n + 2, f[i * 3 + 1]); put16(p + n + 4, f[i * 3 + 2]); put16(p + n + 6, 0);
        put32(p + n + 8, col[i]);
        for (int k = 0; k < 6; k++) put16(p + n + 12 + k * 2, uv8[i * 6 + k]);
        n += BM_MESH_FACE;
    }
    return n;
}

static size_t cart_with(uint8_t *cart, const uint8_t *sec, size_t n, uint32_t type)
{
    static const char lua[] = "-- mesh\n";
    size_t off = BM_HEADER_SIZE + 32;
    memset(cart, 0, off + 16);
    memcpy(cart, "BMCART\0\0", 8);
    put16(cart + 8, 1); put16(cart + 10, BM_HEADER_SIZE); put16(cart + 12, 640); put16(cart + 14, 360);
    cart[16] = BM_FMT_RGB565; cart[17] = 2;
    uint8_t *t = cart + BM_HEADER_SIZE;
    put32(t, BM_SEC_LUA); put32(t + 4, (uint32_t)off); put32(t + 8, sizeof lua - 1); put32(t + 12, 0);
    memcpy(cart + off, lua, sizeof lua - 1);
    size_t off2 = off + 16;
    put32(t + 16, type); put32(t + 20, (uint32_t)off2); put32(t + 24, (uint32_t)n); put32(t + 28, 0);
    memcpy(cart + off2, sec, n);
    size_t len = off2 + n;
    put32(cart + 20, crc32(cart + BM_HEADER_SIZE, (uint32_t)(len - BM_HEADER_SIZE)));
    return len;
}

static void test_mesh(void)
{
    static uint8_t sec[1024], cart[2048];
    static const float quad_v[] = { 0, 0, 0,  0, 1, 0,  1, 1, 0,  1, 0, 0 };
    static const uint16_t quad_f[] = { 0, 1, 2,  0, 2, 3 };
    static const uint32_t quad_c[] = { 0x80000000u, 0x80000000u };
    static const uint16_t quad_uv[] = { 0, 128, 0, 0, 128, 0,   0, 128, 128, 0, 128, 128 };
    static const float tri_v[] = { -1, 0, 0,  0, 2, 0.5f,  1, 0, 0 };
    static const uint16_t tri_f[] = { 0, 1, 2 };
    static const uint32_t tri_c[] = { 0x33CC66 };
    static const uint16_t tri_uv[6] = { 0 };
    size_t n = 8;
    put16(sec, 2); put16(sec + 2, 64); put32(sec + 4, 0);
    n += mesh_model(sec + n, "wall", 4, quad_v, 2, quad_f, quad_c, quad_uv);
    n += mesh_model(sec + n, "roof", 3, tri_v, 1, tri_f, tri_c, tri_uv);
    size_t len = cart_with(cart, sec, n, BM_SEC_MESH);

    bm_cart_t c;
    char err[64] = "";
    CHECK(bm_parse(cart, len, &c, err, sizeof err) == 0, "mesh parse: %s", err);
    CHECK(c.mesh && c.models == 2 && c.mesh_size == n, "mesh section found (%d models)", c.models);
    CHECK(bm_mesh_inset(c.mesh) == 0.25f, "mesh inset");
    bm_model_t m;
    CHECK(bm_mesh_model(c.mesh, c.mesh_size, 0, &m) == 0 && strcmp(m.name, "wall") == 0 &&
          m.nverts == 4 && m.nfaces == 2, "first model");
    uint16_t idx[3];
    uint32_t col;
    float uv[6], xyz[3];
    bm_model_face(&m, 1, idx, &col, uv);
    CHECK(idx[0] == 0 && idx[1] == 2 && idx[2] == 3 && col == 0x80000000u, "face indices and colour");
    CHECK(uv[0] == 0 && uv[1] == 16 && uv[2] == 16 && uv[3] == 0 && uv[4] == 16 && uv[5] == 16, "face uv in pixels");
    CHECK(bm_mesh_model(c.mesh, c.mesh_size, 1, &m) == 0 && strcmp(m.name, "roof") == 0, "second model");
    bm_model_vertex(&m, 1, xyz);
    CHECK(xyz[0] == 0 && xyz[1] == 2 && xyz[2] == 0.5f, "vertex");
    bm_model_face(&m, 0, idx, &col, uv);
    CHECK(col == 0x33CC66, "plain colour");
    CHECK(bm_mesh_model(c.mesh, c.mesh_size, 2, &m) != 0, "no third model");

    /* a vertex index out of range, a short section, a wrong count */
    size_t f0 = 8 + 24 + 4 * 12;            /* first face of "wall" */
    uint8_t save = sec[f0 + 2];
    sec[f0 + 2] = 4;
    len = cart_with(cart, sec, n, BM_SEC_MESH);
    CHECK(bm_parse(cart, len, &c, err, sizeof err) != 0 && strstr(err, "MESH"), "bad index refused");
    sec[f0 + 2] = save;
    len = cart_with(cart, sec, n - 4, BM_SEC_MESH);
    CHECK(bm_parse(cart, len, &c, err, sizeof err) != 0, "short section refused");
    put16(sec, 3);
    len = cart_with(cart, sec, n, BM_SEC_MESH);
    CHECK(bm_parse(cart, len, &c, err, sizeof err) != 0, "wrong model count refused");
    put16(sec, 2);
    /* a section of a type this kernel does not know is ignored */
    len = cart_with(cart, sec, n, 99);
    CHECK(bm_parse(cart, len, &c, err, sizeof err) == 0 && !c.mesh, "unknown section ignored");
}

/* A cartridge written by bm Studio (tests/studio/test_core.js): the
 * kernel's parser takes its models. */
static void test_studio_cart(const char *path)
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    CHECK(d != NULL, "read %s", path);
    if (!d) return;
    bm_cart_t c;
    char err[64] = "";
    CHECK(bm_parse(d, n, &c, err, sizeof err) == 0, "parse the bm Studio cartridge: %s", err);
    CHECK(c.width == 320 && c.sheet8 && c.sheet_w == 256 && c.cover_rgba && c.map_cells, "its sections");
    CHECK(c.models == 2 && bm_mesh_inset(c.mesh) == 0.5f, "its models (%d)", c.models);
    bm_model_t m;
    CHECK(bm_mesh_model(c.mesh, c.mesh_size, 0, &m) == 0 && strcmp(m.name, "house") == 0 && m.nfaces == 21,
          "house: %u triangles", m.nfaces);
    int textured = 0;
    for (int f = 0; f < m.nfaces; f++) {
        uint16_t idx[3];
        uint32_t col;
        float uv[6];
        bm_model_face(&m, f, idx, &col, uv);
        textured += (col & 0x80000000u) != 0;
    }
    CHECK(textured == 20, "textured faces: %d", textured);
    free(d);
}

/* A cartridge with a skeleton written by bm Studio's core (test_core.js):
 * the kernel reads the rig, its clips and keys; broken rigs are refused. */
static void test_anim_cart(const char *path)
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    CHECK(d != NULL, "read %s", path);
    if (!d) return;
    bm_cart_t c;
    char err[64] = "";
    CHECK(bm_parse(d, n, &c, err, sizeof err) == 0, "parse the cartridge with a skeleton: %s", err);
    bm_rig_t r;
    CHECK(c.anim && bm_anim_rig(c.anim, c.anim_size, "figure", &r) == 0, "the rig of \"figure\"");
    CHECK(bm_anim_rig(c.anim, c.anim_size, "nobody", &r) != 0, "no rig for another model");
    bm_anim_rig(c.anim, c.anim_size, "figure", &r);
    bm_model_t m;
    bm_mesh_model(c.mesh, c.mesh_size, 0, &m);
    CHECK(r.nbones == 2 && r.nclips == 2 && r.nverts == m.nverts, "bones %u clips %u vertices %u/%u", r.nbones, r.nclips,
          r.nverts, m.nverts);
    char name[BM_MODEL_NAME + 1];
    int parent;
    float head[3], tail[3];
    bm_rig_bone(&r, 1, name, &parent, head, tail);
    CHECK(strcmp(name, "arm.R") == 0 && parent == 0 && head[0] == 1 && head[1] == 1 && tail[0] == 2, "bone 1");
    bm_clip_t cl;
    CHECK(bm_rig_clip(&r, 0, &cl) == 0 && strcmp(cl.name, "wave") == 0 && cl.nkeys == 2 && cl.mode == 1 && cl.loop &&
          cl.length == 1.0f, "clip wave");
    float q[2][4], t[2][3];
    float time = bm_clip_key(&cl, 2, 1, q, t);
    CHECK(time == 0.5f && t[1][1] == 0.25f && q[1][2] > 0.7f && q[0][3] == 1, "key 1 of wave: %g", time);
    CHECK(bm_rig_clip(&r, 1, &cl) == 0 && strcmp(cl.name, "still") == 0 && cl.mode == 2 && !cl.loop, "clip still");
    CHECK(bm_rig_clip(&r, 2, &cl) != 0, "no third clip");
    int arm = 0;
    for (int i = 0; i < r.nverts; i++) arm += r.vbones[i] == 1;
    CHECK(arm == 8, "8 vertices follow the arm (%d)", arm);
    /* a bone whose parent comes after it is refused */
    uint8_t *b1 = (uint8_t *)r.bones + BM_BONE_SIZE;
    b1[16] = 5;
    put32(d + 20, crc32(d + BM_HEADER_SIZE, (uint32_t)(n - BM_HEADER_SIZE)));
    CHECK(bm_parse(d, n, &c, err, sizeof err) != 0 && strstr(err, "ANIM"), "bad parent refused");
    free(d);
}

int main(int argc, char **argv)
{
    test_primitives();
    test_sprites();
    test_text();
    test_light();
    test_3d();
    test_format(argc > 1 ? argv[1] : "build/demo.bm");
    test_sheet8();
    test_mesh();
    if (argc > 2)
        test_studio_cart(argv[2]);
    if (argc > 3)
        test_anim_cart(argv[3]);
    printf("bm: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
