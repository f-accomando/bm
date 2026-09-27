/* Host tests for the .b33 parser and the RGB565 drawing library. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "b33/b33.h"
#include "b33/gfx16.h"
#include "b33/r3d.h"

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

    /* 2D triangle */
    g16_cls(&g, 0);
    g16_tri(&g, 10, 10, 50, 10, 10, 50, 0xFFFF);
    CHECK(big[12 * 640 + 12] == 0xFFFF && big[45 * 640 + 45] == 0, "2D triangle");
    g16_tri(&g, -100, -100, 1000, -50, 300, 1000, 0x1234);   /* huge, clipped */
    CHECK(big[180 * 640 + 320] == 0x1234, "clipped big triangle");

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
    b33_cart_t c;
    char err[64] = "";
    CHECK(b33_parse(d, n, &c, err, sizeof err) == 0, "parse demo: %s", err);
    CHECK(strcmp(c.title, "bm33 native demo") == 0, "title '%s'", c.title);
    CHECK(c.width == 640 && c.height == 360 && c.pixel_format == 1, "video mode");
    CHECK(c.lua && c.lua_size > 100 && memcmp(c.lua, "--", 2) == 0, "lua section");
    CHECK(c.sheet_w == 128 && c.sheet_h == 128 && c.sheet_rgba, "sheet");
    CHECK(c.map_w == 160 && c.map_h == 90 && c.map_cells, "map");

    d[n - 1] ^= 1;
    CHECK(b33_parse(d, n, &c, err, sizeof err) != 0 && strstr(err, "CRC"), "corruption detected");
    d[n - 1] ^= 1;
    memcpy(d, "BM33CARX", 8);
    CHECK(b33_parse(d, n, &c, err, sizeof err) != 0, "bad magic rejected");
    CHECK(b33_parse(d, 10, &c, err, sizeof err) != 0, "short file rejected");
    free(d);
}

int main(int argc, char **argv)
{
    test_primitives();
    test_sprites();
    test_text();
    test_3d();
    test_format(argc > 1 ? argv[1] : "build/demo.b33");
    printf("b33: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
