#include "gputest.h"
#include "crumbs.h"
#include "input.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "gfx/console.h"
#include "bm/r3d.h"
#include "gfx/font.h"
#include "gpu/gpu3d.h"
#include "gpu/shaders.h"
#include "gpu/v3d.h"
#include "lib/printf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 360
#define TILES_X ((W + V3D_TILE - 1) / V3D_TILE)
#define TILES_Y ((H + V3D_TILE - 1) / V3D_TILE)
#define TIMEOUT_US 250000

/* a vertex as the NV shader state wants it: screen x and y in 12.4,
 * z (0 near .. 1 far), 1/w, then the varyings (here a colour) */
typedef struct {
    int16_t x, y;
    float z, inv_w;
    float c[3];
} nv_vert_t;

#define MAX_VERTS 60000

/* Everything the V3D reads or writes, in one block: the binner wants its
 * memory (tile state, tile lists, overflow) within one 256 MiB window. */
#define TSDA_SIZE     4096          /* 48 bytes per tile */
#define ALLOC_SIZE    (2u << 20)    /* tile lists */
#define OVERFLOW_SIZE (1u << 20)
#define BCL_SIZE      4096
#define RCL_SIZE      8192
#define REC_SIZE      64
#define CODE_SIZE     1024          /* fs_colour, then fs_texture at +512 */
#define TEX_SIZE      (64 * 64 * 4) /* RGBA32R texture, and its parameters after it */
#define TEX_PARAMS    16
#define VERTS_SIZE    ((MAX_VERTS * sizeof(nv_vert_t) + 4095) & ~4095u)
#define TARGET_SIZE   (W * H * 2)
#define BLOCK_SIZE    (TSDA_SIZE + ALLOC_SIZE + OVERFLOW_SIZE + BCL_SIZE + RCL_SIZE + REC_SIZE + \
                       CODE_SIZE + VERTS_SIZE + TARGET_SIZE + TEX_SIZE + 4096)

static struct {
    uint8_t *block;
    uint8_t *tsda, *alloc, *overflow, *bcl, *rcl, *rec;
    uint32_t *code;
    nv_vert_t *verts;
    uint16_t *target;               /* 640x360 RGB565, in RAM */
    uint32_t *tex;                  /* 64x64 RGBA32R, 4 KiB aligned; its P0 P1 after it */
    uint32_t bcl_end, rcl_end;      /* bus addresses of the lists' ends */
    int nverts;
    int red_a;                      /* byte a of the tile buffer lands on red in BGR565 */
} m;

static int failed;

static void step(const char *what)
{
    crumb("GPU test", what);
    kprintf("  %-36s", what);
    timer_delay_ms(40);             /* on the display before the V3D starts */
}

static void fail(const char *why)
{
    kprintf("\x1b[91mFAILED\x1b[0m %s\n", why);
    failed = 1;
}

static void fail_dump(const char *why)
{
    fail(why);
    static char buf[640];
    v3d_dump(buf, sizeof buf);
    kprintf("%s", buf);
}

/* ---------------------------------------------------------------- memory */

static uint8_t *block_alloc(void)
{
    uint8_t *b = aligned_alloc(4096, BLOCK_SIZE);
    if (b && (v3d_bus(b) >> 28) != (v3d_bus(b + BLOCK_SIZE - 1) >> 28)) {
        uint8_t *again = aligned_alloc(4096, BLOCK_SIZE);   /* across 256 MiB: another */
        free(b);
        b = again;
        if (b && (v3d_bus(b) >> 28) != (v3d_bus(b + BLOCK_SIZE - 1) >> 28)) {
            free(b);
            b = NULL;
        }
    }
    return b;
}

static int mem_init(void)
{
    uint8_t *b = block_alloc();
    if (!b)
        return -1;
    m.block = b;
    m.tsda = b;
    m.alloc = m.tsda + TSDA_SIZE;
    m.overflow = m.alloc + ALLOC_SIZE;
    m.bcl = m.overflow + OVERFLOW_SIZE;
    m.rcl = m.bcl + BCL_SIZE;
    m.rec = m.rcl + RCL_SIZE;
    m.code = (uint32_t *)(m.rec + REC_SIZE);
    m.verts = (nv_vert_t *)((uint8_t *)m.code + CODE_SIZE);
    m.target = (uint16_t *)((uint8_t *)m.verts + VERTS_SIZE);
    m.tex = (uint32_t *)(((uintptr_t)m.target + TARGET_SIZE + 4095) & ~(uintptr_t)4095);
    memset(m.tsda, 0, TSDA_SIZE);
    memcpy(m.code, fs_colour, sizeof fs_colour);
    memcpy(m.code + 128, fs_texture, sizeof fs_texture);
    v3d_set_overflow(v3d_bus(m.overflow), OVERFLOW_SIZE);
    return 0;
}

/* ---------------------------------------------------------------- lists */

/* Rendering list: clear colour and depth, then every tile: its tile list
 * from the binner (when there is one) and the store to the frame at bus
 * address fb (w x h, format = V3D_RENDER_*). */
static void rcl_build(int with_bin, uint32_t fb, int w, int h, uint16_t format, uint32_t clear_rgba)
{
    const int tx = (w + V3D_TILE - 1) / V3D_TILE, ty = (h + V3D_TILE - 1) / V3D_TILE;
    v3d_cl_t cl;
    v3d_cl_init(&cl, m.rcl, RCL_SIZE);
    v3d_cl_u8(&cl, V3D_CLEAR_COLORS);
    v3d_cl_u32(&cl, clear_rgba);
    v3d_cl_u32(&cl, clear_rgba);
    v3d_cl_u32(&cl, 0x00FFFFFF);            /* depth 1.0 (far), VG mask 0 */
    v3d_cl_u8(&cl, 0);                      /* stencil */
    v3d_cl_u8(&cl, V3D_TILE_RENDERING_MODE_CONFIG);
    v3d_cl_u32(&cl, fb);
    v3d_cl_u16(&cl, (uint16_t)w);
    v3d_cl_u16(&cl, (uint16_t)h);
    v3d_cl_u16(&cl, format);
    /* a store of nothing: the tile buffer takes the clear values */
    v3d_cl_u8(&cl, V3D_TILE_COORDINATES);
    v3d_cl_u8(&cl, 0);
    v3d_cl_u8(&cl, 0);
    v3d_cl_u8(&cl, V3D_STORE_TILE_BUFFER_GENERAL);
    v3d_cl_u16(&cl, 0);
    v3d_cl_u32(&cl, 0);
    for (int y = 0; y < ty; y++)
        for (int x = 0; x < tx; x++) {
            v3d_cl_u8(&cl, V3D_TILE_COORDINATES);
            v3d_cl_u8(&cl, (uint8_t)x);
            v3d_cl_u8(&cl, (uint8_t)y);
            if (with_bin) {
                v3d_cl_u8(&cl, V3D_BRANCH_TO_SUB_LIST);
                v3d_cl_u32(&cl, v3d_bus(m.alloc) + (uint32_t)(y * tx + x) * 32);
            }
            v3d_cl_u8(&cl, x == tx - 1 && y == ty - 1 ? V3D_STORE_MS_TILE_BUFFER_EOF
                                                     : V3D_STORE_MS_TILE_BUFFER);
        }
    m.rcl_end = v3d_bus(cl.p);
}

/* Binning list: the vertices m.verts[0, nverts) as triangles, coloured by
 * fs_colour (3 varyings) or, with tex, textured by fs_texture (s and t in
 * the first two of the 3 varyings, the texture parameters as uniforms);
 * cfg = CONFIGURATION_BITS (first byte, rest). */
static void bcl_build_shader(int w, int h, uint8_t cfg8, uint16_t cfg16, int tex)
{
    const int tx = (w + V3D_TILE - 1) / V3D_TILE, ty = (h + V3D_TILE - 1) / V3D_TILE;
    /* shader record (NV): flags, vertex stride, uniforms, varyings, code,
     * uniforms address, vertices */
    uint8_t *r = m.rec;
    r[0] = 0;
    r[1] = sizeof(nv_vert_t);
    r[2] = tex ? 2 : 0;
    r[3] = tex ? 2 : 3;
    uint32_t a = v3d_bus(tex ? m.code + 128 : m.code);
    memcpy(r + 4, &a, 4);
    a = tex ? v3d_bus(m.tex + 64 * 64) : 0;
    memcpy(r + 8, &a, 4);
    a = v3d_bus(m.verts);
    memcpy(r + 12, &a, 4);

    v3d_cl_t cl;
    v3d_cl_init(&cl, m.bcl, BCL_SIZE);
    v3d_cl_u8(&cl, V3D_TILE_BINNING_MODE_CONFIG);
    v3d_cl_u32(&cl, v3d_bus(m.alloc));
    v3d_cl_u32(&cl, ALLOC_SIZE);
    v3d_cl_u32(&cl, v3d_bus(m.tsda));
    v3d_cl_u8(&cl, (uint8_t)tx);
    v3d_cl_u8(&cl, (uint8_t)ty);
    /* tile state set up by the binner, first blocks of 32 bytes (the
     * rendering list branches to them), then blocks of 128 */
    v3d_cl_u8(&cl, V3D_BIN_AUTO_INIT_TSDA | 0 << 3 | 2 << 5);
    v3d_cl_u8(&cl, V3D_START_TILE_BINNING);
    v3d_cl_u8(&cl, V3D_CLIP_WINDOW);
    v3d_cl_u16(&cl, 0);
    v3d_cl_u16(&cl, 0);
    v3d_cl_u16(&cl, (uint16_t)w);
    v3d_cl_u16(&cl, (uint16_t)h);
    v3d_cl_u8(&cl, V3D_CONFIGURATION_BITS);
    v3d_cl_u8(&cl, cfg8);
    v3d_cl_u16(&cl, cfg16);
    v3d_cl_u8(&cl, V3D_VIEWPORT_OFFSET);
    v3d_cl_u16(&cl, 0);
    v3d_cl_u16(&cl, 0);
    v3d_cl_u8(&cl, V3D_NV_SHADER_STATE);
    v3d_cl_u32(&cl, v3d_bus(m.rec));
    v3d_cl_u8(&cl, V3D_VERTEX_ARRAY_PRIMITIVES);
    v3d_cl_u8(&cl, 4);                      /* triangles */
    v3d_cl_u32(&cl, (uint32_t)m.nverts);
    v3d_cl_u32(&cl, 0);
    v3d_cl_u8(&cl, V3D_FLUSH);              /* ends the tile lists */
    v3d_cl_u8(&cl, V3D_NOP);
    m.bcl_end = v3d_bus(cl.p);
    /* a stale tile state could be taken by the binner (Linux clears it
     * before every job too) */
    memset(m.tsda, 0, TSDA_SIZE);
}

static void bcl_build(int w, int h, uint8_t cfg8, uint16_t cfg16)
{
    bcl_build_shader(w, h, cfg8, cfg16, 0);
}

/* a vertex at screen (x, y), depth z, colour 0xRRGGBB */
static void vert(float x, float y, float z, uint32_t rgb)
{
    if (m.nverts >= MAX_VERTS)
        return;
    nv_vert_t *v = &m.verts[m.nverts++];
    v->x = (int16_t)(x * 16.0f);
    v->y = (int16_t)(y * 16.0f);
    v->z = z;
    v->inv_w = 1.0f;
    float r = (rgb >> 16 & 255) / 255.0f, g = (rgb >> 8 & 255) / 255.0f, b = (rgb & 255) / 255.0f;
    v->c[0] = m.red_a ? r : b;
    v->c[1] = g;
    v->c[2] = m.red_a ? b : r;
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint32_t rgb)
{
    vert(x0, y0, z, rgb);
    vert(x1, y1, z, rgb);
    vert(x2, y2, z, rgb);
}

static int run(int with_bin, uint32_t *bin_us, uint32_t *rnd_us)
{
    return v3d_run(with_bin ? v3d_bus(m.bcl) : 0, with_bin ? m.bcl_end : 0, v3d_bus(m.rcl), m.rcl_end,
                   TIMEOUT_US, bin_us, rnd_us);
}

/* RGB565 pixel of the target as 0xRRGGBB (low bits filled) */
static uint32_t px(int x, int y)
{
    uint16_t c = m.target[y * W + x];
    uint32_t r = c >> 11, g = c >> 5 & 63, b = c & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

/* the channel (16 red, 8 green, 0 blue) is strong and the others weak */
static int mostly(uint32_t c, int shift)
{
    for (int s = 0; s < 24; s += 8)
        if (s == shift ? (int)(c >> s & 255) < 150 : (int)(c >> s & 255) > 110)
            return 0;
    return 1;
}

static int near(uint32_t a, uint32_t b, int tol)
{
    for (int s = 0; s < 24; s += 8) {
        int d = (int)(a >> s & 255) - (int)(b >> s & 255);
        if (d > tol || d < -tol)
            return 0;
    }
    return 1;
}

static uint32_t rng = 12345;
static float rnd(float lo, float hi)
{
    rng = rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(rng >> 8) / 16777216.0f;
}

/* ---------------------------------------------------------------- steps */

static int step_clear(void)
{
    step("3 clear (rendering list only)");
    /* clear colour 0xAABBGGRR as the bytes d c b a: once with byte a
     * full, once with byte c, to see where they land in BGR565 */
    uint16_t got[2];
    uint32_t us = 0;
    static const uint32_t colours[2] = { 0x000000FFu, 0x00FF0000u };
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < W * H; i++)
            m.target[i] = 0x1234;
        rcl_build(0, v3d_bus(m.target), W, H, V3D_RENDER_BGR565, colours[k]);
        if (run(0, NULL, &us) != 0) {
            fail_dump("no end of frame");
            return -1;
        }
        got[k] = m.target[0];
        for (int i = 0; i < W * H; i++)
            if (m.target[i] != got[k]) {
                char why[80];
                ksnprintf(why, sizeof why, "pixel %d is %04x, pixel 0 is %04x", i, m.target[i], got[k]);
                fail(why);
                return -1;
            }
    }
    if (got[0] == 0xF800 && got[1] == 0x001F) {
        m.red_a = 1;
    } else if (got[0] == 0x001F && got[1] == 0xF800) {
        m.red_a = 0;
    } else {
        char why[80];
        ksnprintf(why, sizeof why, "byte a -> %04x, byte c -> %04x (expected f800 / 001f)", got[0], got[1]);
        fail(why);
        return -1;
    }
    kprintf("ok  %lu us, byte a = %s\n", us, m.red_a ? "red" : "blue");
    return 0;
}

static int step_triangle(void)
{
    step("4 one triangle (binning, shader)");
    m.nverts = 0;
    vert(320, 40, 0.5f, 0xFF0000);
    vert(80, 320, 0.5f, 0x00FF00);
    vert(560, 320, 0.5f, 0x0000FF);
    bcl_build(W, H, V3D_CFG_FRONT | V3D_CFG_BACK, V3D_CFG_DEPTH(7));
    rcl_build(1, v3d_bus(m.target), W, H, V3D_RENDER_BGR565, 0xFF202020u);
    uint32_t bus_us, rus;
    if (run(1, &bus_us, &rus) != 0) {
        fail_dump("the job did not end");
        return -1;
    }
    /* near each corner its colour, the middle a mix, outside the clear grey */
    uint32_t top = px(320, 50), left = px(95, 312), right = px(545, 312), mid = px(320, 230),
             out = px(20, 20);
    int ok = mostly(top, 16) && mostly(left, 8) && mostly(right, 0) &&
             near(out, 0x202020, 8) && (mid >> 16 & 255) > 30 && (mid >> 8 & 255) > 30 && (mid & 255) > 30;
    if (!ok) {
        char why[120];
        ksnprintf(why, sizeof why, "corners %06lx %06lx %06lx, middle %06lx, outside %06lx", top, left,
                  right, mid, out);
        fail(why);
        return -1;
    }
    kprintf("ok  bin %lu us, render %lu us\n", bus_us, rus);
    return 0;
}

static int step_depth(void)
{
    step("5 depth test (24-bit z in the chip)");
    /* red at z 0.25 and green at z 0.75 overlap: red must win whatever
     * the order */
    for (int order = 0; order < 2; order++) {
        m.nverts = 0;
        for (int k = 0; k < 2; k++) {
            int red = (k == 0) == (order == 0);
            float x = red ? 200 : 260;
            tri(x, 80, x + 240, 80, x, 300, red ? 0.25f : 0.75f, red ? 0xFF0000 : 0x00FF00);
        }
        bcl_build(W, H, V3D_CFG_FRONT | V3D_CFG_BACK,
                  V3D_CFG_DEPTH(3) | V3D_CFG_Z_UPDATE | V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE);
        rcl_build(1, v3d_bus(m.target), W, H, V3D_RENDER_BGR565, 0xFF000000u);
        if (run(1, NULL, NULL) != 0) {
            fail_dump("the job did not end");
            return -1;
        }
        uint32_t both = px(300, 120), green = px(470, 100), red = px(210, 250);
        if (!near(both, 0xFF0000, 16) || !near(green, 0x00FF00, 16) || !near(red, 0xFF0000, 16)) {
            char why[100];
            ksnprintf(why, sizeof why, "order %d: overlap %06lx, green %06lx, red %06lx", order, both,
                      green, red);
            fail(why);
            return -1;
        }
    }
    kprintf("ok\n");
    return 0;
}

static int step_speed_small(void)
{
    step("6 speed: 20000 small triangles");
    uint32_t t0 = timer_ticks();
    m.nverts = 0;
    for (int i = 0; i < 20000; i++) {
        float x = rnd(0, W - 12), y = rnd(0, H - 12), z = rnd(0.05f, 0.95f);
        uint32_t c = (uint32_t)(rnd(0, 1) * 16777215.0f);
        tri(x, y, x + 12, y + 2, x + 3, y + 11, z, c);
    }
    uint32_t cpu = timer_ticks() - t0;
    bcl_build(W, H, V3D_CFG_FRONT | V3D_CFG_BACK,
              V3D_CFG_DEPTH(3) | V3D_CFG_Z_UPDATE | V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE);
    rcl_build(1, v3d_bus(m.target), W, H, V3D_RENDER_BGR565, 0xFF000000u);
    uint32_t bus_us, rus;
    if (run(1, &bus_us, &rus) != 0) {
        fail_dump("the job did not end");
        return -1;
    }
    uint32_t total = bus_us + rus;
    kprintf("ok  ARM %lu us, bin %lu us, render %lu us: %lu k tri/s\n", cpu, bus_us, rus,
            total ? 20000u * 1000u / total : 0);
    return 0;
}

static int step_speed_fill(void)
{
    step("7 speed: 20 full screens");
    m.nverts = 0;
    for (int i = 0; i < 20; i++) {
        float z = 0.9f - i * 0.04f;
        uint32_t c = 0x102030u * (uint32_t)(i + 1);
        tri(0, 0, W, 0, 0, H, z, c);
        tri(W, 0, W, H, 0, H, z, c);
    }
    bcl_build(W, H, V3D_CFG_FRONT | V3D_CFG_BACK,
              V3D_CFG_DEPTH(3) | V3D_CFG_Z_UPDATE | V3D_CFG_EARLY_Z | V3D_CFG_EARLY_Z_UPDATE);
    rcl_build(1, v3d_bus(m.target), W, H, V3D_RENDER_BGR565, 0xFF000000u);
    uint32_t bus_us, rus;
    if (run(1, &bus_us, &rus) != 0) {
        fail_dump("the job did not end");
        return -1;
    }
    uint32_t pixels = 20u * W * H;
    kprintf("ok  bin %lu us, render %lu us: %lu Mpixel/s\n", bus_us, rus, rus ? pixels / rus : 0);
    return 0;
}

/* A quad textured by the TMU: a 64x64 RGBA32R texture (raster order, 32
 * bits a texel, as kumaashi's demo on the Pi Zero W), left half 0xFF0000FF
 * and right half 0xFFFF0000 as words; on screen they must give red and
 * blue (which is which says the byte order of the TMU). */
static int step_texture(void)
{
    step("8 texture (TMU), nearest texel");
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            m.tex[y * 64 + x] = x < 32 ? 0xFF0000FFu : 0xFFFF0000u;
    /* P0: base (4 KiB units), type RGBA32R = 16: low 4 bits 0, bit 4 in P1;
     * P1: type bit 4, height, width, nearest magnification and
     * minification, clamp to edge (1) in s and t */
    uint32_t *p = m.tex + 64 * 64;
    p[0] = v3d_bus(m.tex) & ~0xFFFu;
    p[1] = 1u << 31 | 64u << 20 | 64u << 8 | 1u << 7 | 1u << 4 | 1u << 2 | 1u;
    m.nverts = 0;
    /* the colour slots carry s and t (the shader reads 2 of the 3) */
    static const float q[6][4] = { { 120, 60, 0, 0 }, { 520, 60, 1, 0 }, { 120, 300, 0, 1 },
                                   { 520, 60, 1, 0 }, { 520, 300, 1, 1 }, { 120, 300, 0, 1 } };
    for (int i = 0; i < 6; i++) {
        nv_vert_t *v = &m.verts[m.nverts++];
        v->x = (int16_t)(q[i][0] * 16);
        v->y = (int16_t)(q[i][1] * 16);
        v->z = 0.5f;
        v->inv_w = 1.0f;
        v->c[0] = q[i][2];
        v->c[1] = q[i][3];
        v->c[2] = 0;
    }
    bcl_build_shader(W, H, V3D_CFG_FRONT | V3D_CFG_BACK, V3D_CFG_DEPTH(7), 1);
    rcl_build(1, v3d_bus(m.target), W, H, V3D_RENDER_BGR565, 0xFF000000u);
    uint32_t rus;
    if (run(1, NULL, &rus) != 0) {
        fail_dump("the job did not end");
        return -1;
    }
    uint32_t left = px(200, 180), right = px(440, 180), out = px(20, 20);
    int same = mostly(left, m.red_a ? 16 : 0) && mostly(right, m.red_a ? 0 : 16);
    int swapped = mostly(left, m.red_a ? 0 : 16) && mostly(right, m.red_a ? 16 : 0);
    if (!(same || swapped) || !near(out, 0, 8)) {
        char why[100];
        ksnprintf(why, sizeof why, "left %06lx, right %06lx, outside %06lx", left, right, out);
        fail(why);
        return -1;
    }
    kprintf("ok  render %lu us, texel bytes %s the tile buffer's\n", rus, same ? "as" : "swapped against");
    return 0;
}

/* a picture drawn by the GPU straight into the console's page */
static int step_screen(framebuffer_t *fb)
{
    step("9 on the screen (RGBA8888)");
    if (fb->depth != 32 || fb->width != W || fb->height != H || fb->pitch != W * 4) {
        kprintf("skipped (console %lux%lu, %lu bpp)\n", fb->width, fb->height, fb->depth);
        return 0;
    }
    /* RGBA8888 stores byte a first: the console's low byte is red when
     * is_rgb, else blue (the colours are swapped for vert()) */
    int saved = m.red_a;
    m.red_a = fb->is_rgb != 0;
    m.nverts = 0;
    for (int i = 0; i < 48; i++) {
        float a0 = i * 6.2831853f / 48, a1 = (i + 1) * 6.2831853f / 48;
        uint32_t c = (uint32_t)((i * 5) & 255) << 16 | (uint32_t)((255 - i * 5) & 255) << 8 | 0xA0;
        vert(320, 180, 0.5f, 0xFFFFFF);
        vert(320 + 300 * cosf(a0), 180 + 170 * sinf(a0), 0.5f, c);
        vert(320 + 300 * cosf(a1), 180 + 170 * sinf(a1), 0.5f, c);
    }
    m.red_a = saved;
    bcl_build(W, H, V3D_CFG_FRONT | V3D_CFG_BACK, V3D_CFG_DEPTH(7));
    rcl_build(1, fb->bus + (uint32_t)(fb->base - fb->mem), W, H, V3D_RENDER_RGBA8888, 0xFF302010u);
    uint32_t rus;
    if (run(1, NULL, &rus) != 0) {
        console_suspend(1);
        console_suspend(0);
        fail_dump("the job did not end");
        return -1;
    }
    /* the picture stays until a key or 10 s */
    uint32_t t0 = timer_ticks();
    input_flush();
    while (timer_ticks() - t0 < 10000000u && input_key() < 0)
        ;
    console_suspend(1);
    console_suspend(0);
    kprintf("ok  render %lu us\n", rus);
    return 0;
}

/* ---------------------------------------------------------------- step 10 */

/* The 3D of the games on the GPU (gpu3d, the backend of r3d): one scene
 * drawn by the ARM's rasterizer and by the GPU into two 640x360 RGB565
 * pages, timed, compared, and shown side by side (half size). */
static g16_sheet_t sc_sheet;
static r3d_mesh_t sc_sphere, sc_cube, sc_quad, sc_floor;

/* 128x64: four 32x32 checkers; the second row of squares has its left
 * column transparent (the alpha shader) */
static int scene_init(void)
{
    static const uint32_t pairs[4][2] = { { 0xE02020, 0xF0E040 }, { 0x2040E0, 0xF0F0F0 },
                                          { 0x20C040, 0x101010 }, { 0x808080, 0xF08020 } };
    if (g16_sheet_alloc(&sc_sheet, 128, 64) != 0 || r3d_mesh_sphere(&sc_sphere, 10, 16, 0x4080FF, 0xFFC040) != 0 ||
        r3d_mesh_cube(&sc_cube, 0x60C060) != 0)
        return -1;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++) {
            int t = x / 32, lx = x % 32, ly = y % 32;
            g16_sheet_set(&sc_sheet, x, y, g16_rgb24(pairs[t][((lx / 8) ^ (ly / 8)) & 1]),
                          !(y >= 32 && lx < 8));
        }
    for (int cy = 0; cy < 8; cy++)
        for (int cx = 0; cx < 16; cx++)
            g16_sheet_update_cell(&sc_sheet, cx, cy);
    r3d_mesh_t *q[2] = { &sc_quad, &sc_floor };
    for (int k = 0; k < 2; k++) {
        if (r3d_mesh_alloc(q[k], 4, 2) != 0 || r3d_mesh_alloc_uv(q[k]) != 0)
            return -1;
        q[k]->verts[0] = (v3_t){ -1, -1, 0 }; q[k]->verts[1] = (v3_t){ 1, -1, 0 };
        q[k]->verts[2] = (v3_t){ 1, 1, 0 };   q[k]->verts[3] = (v3_t){ -1, 1, 0 };
        static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
        memcpy(q[k]->faces, f, sizeof f);
        const float u0 = k ? 64 : 0, v0 = k ? 0 : 32, e = 31.5f;
        const float uv[12] = { u0, v0 + e, u0 + e, v0, u0 + e, v0 + e,   u0, v0 + e, u0, v0, u0 + e, v0 };
        memcpy(q[k]->uv, uv, sizeof uv);
        q[k]->colors[0] = q[k]->colors[1] = R3D_TEXTURED;
        q[k]->tex = &sc_sheet;
        r3d_mesh_normals(q[k]);
    }
    return 0;
}

static void scene_free(void)
{
    r3d_mesh_free(&sc_sphere);
    r3d_mesh_free(&sc_cube);
    r3d_mesh_free(&sc_quad);
    r3d_mesh_free(&sc_floor);
    g16_sheet_free(&sc_sheet);
}

/* the 3D of steps 10 and 13: a textured floor without depth, 24 spheres
 * (flat and smooth), a cube, a quad with transparent squares */
static void scene_3d(r3d_t *r)
{
    r3d_zclear(r);
    r3d_camera(r, 0, 1.5f, -7, 0, -0.15f, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_draw_flags(r, &sc_floor, (v3_t){ 0, -1.2f, 4 }, 1.5707963f, 0, 0, 9, R3D_NOZ);
    for (int i = 0; i < 24; i++)
        r3d_draw_flags(r, &sc_sphere, (v3_t){ -4.5f + 1.8f * (float)(i % 6), -0.6f + 1.1f * (float)(i / 6) * 0.8f,
                                              (float)(i / 6) * 1.5f }, 0.3f * (float)i, 0.5f, 0, 0.55f,
                       i & 1 ? R3D_SMOOTH : 0);
    r3d_draw_flags(r, &sc_cube, (v3_t){ 0.3f, 0.2f, -1.5f }, 0.2f, 0.4f, 0, 0.6f, 0);
    r3d_draw_flags(r, &sc_quad, (v3_t){ -1.2f, 0.4f, -2.5f }, 0, 0.3f, 0, 0.8f, 0);
}

/* step 10: the 3D over a page with 2D on it (a bar), which must stay */
static void scene_draw(g16_t *g, r3d_t *r, int gpu)
{
    g16_cls(g, g16_rgb(30, 20, 50));
    g16_rectfill(g, 0, 0, W, 12, g16_rgb(200, 200, 0));
    scene_3d(r);
    if (gpu)
        gpu3d_flush(g, 0);
}

/* step 13: the 3D over a cleared page (the GPU's job starts from the
 * colour: MSAA even where the page cannot be loaded into its samples) */
static void scene_aa(g16_t *g, r3d_t *r, int gpu)
{
    g16_cls(g, g16_rgb(30, 20, 50));
    if (gpu)
        gpu3d_page(1, g16_rgb(30, 20, 50));
    scene_3d(r);
    if (gpu)
        gpu3d_flush(g, 0);
}

/* 3D, 2D over it, then 3D partly behind the first: the depth of the first
 * part, stored by the GPU and loaded again, must hide the second */
static void scene_split(g16_t *g, r3d_t *r, int gpu)
{
    g16_cls(g, g16_rgb(30, 20, 50));
    g16_rectfill(g, 0, 0, W, 12, g16_rgb(200, 200, 0));
    r3d_zclear(r);
    r3d_camera(r, 0, 0, -6, 0, 0, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_draw_flags(r, &sc_sphere, (v3_t){ 0, 0, 0 }, 0, 0, 0, 1.6f, R3D_SMOOTH);
    if (gpu)
        gpu3d_flush(g, 1);                  /* as the runtime before 2D */
    g16_rectfill(g, 80, 250, 260, 50, g16_rgb(20, 200, 90));
    r3d_draw_flags(r, &sc_cube, (v3_t){ 1.0f, 0.3f, 1.5f }, 0.3f, 0.5f, 0, 1.4f, 0);
    r3d_draw_flags(r, &sc_quad, (v3_t){ -1.3f, -0.5f, 1.0f }, 0, 0.4f, 0, 0.9f, 0);
    if (gpu)
        gpu3d_flush(g, 0);
}

static int differs(uint16_t a, uint16_t b)
{
    uint32_t ca = g16_to_rgb24(a), cb = g16_to_rgb24(b);
    for (int k = 0; k < 24; k += 8) {
        int d = (int)(ca >> k & 255) - (int)(cb >> k & 255);
        if (d > 48 || d < -48)
            return 1;
    }
    return 0;
}

/* A scene drawn by the ARM's rasterizer and by the GPU backend into two
 * 640x360 RGB565 pages (twice: the second time textures are made, caches
 * warm, and the backend knows what the scene needs), timed, compared, and
 * shown side by side (half size) until a key or 10 s. */
static int step_compare(framebuffer_t *fb, const char *what, void (*scene)(g16_t *, r3d_t *, int))
{
    step(what);
    if (gpu3d_init() != 0) {
        fail(gpu3d_status());
        return -1;
    }
    gpu3d_set_msaa(0);                      /* the GPU as the ARM draws: no smoothing */
    uint16_t *pg[2] = { aligned_alloc(64, W * H * 2), aligned_alloc(64, W * H * 2) };
    r3d_t r;
    g16_t g;
    uint32_t us[2] = { 0, 0 };
    gpu3d_stats_t st;
    memset(&st, 0, sizeof st);
    int err = !pg[0] || !pg[1] || scene_init() != 0;
    for (int pass = 0; pass < 2 && !err; pass++) {
        g16_target(&g, pg[pass], W, W, H, &font_console_8x16);
        if (r3d_init(&r, &g) != 0) {
            err = 1;
            break;
        }
        r.backend = pass ? gpu3d_backend() : NULL;
        for (int k = 0; k < 2; k++) {
            uint32_t t0 = timer_ticks();
            scene(&g, &r, pass);
            if (pass && gpu3d_failed())
                err = 2;
            us[pass] = timer_ticks() - t0;
            if (pass && k == 0)
                gpu3d_take_stats(&st);
        }
        if (pass)
            gpu3d_take_stats(&st);
        r3d_free(&r);
    }
    if (err) {
        fail(err == 2 ? gpu3d_status() : "no memory for the scene");
    } else {
        int differ = 0;
        for (int i = 0; i < W * H; i++)
            differ += differs(pg[0][i], pg[1][i]);
        const int permille = (int)((int64_t)differ * 1000 / (W * H));
        const int kept = !differs(pg[1][5 * W + 5], g16_rgb(200, 200, 0));
        if (permille > 60 || !kept) {
            char why[100];
            ksnprintf(why, sizeof why, "%d.%d%% of the pixels differ%s", permille / 10, permille % 10,
                      kept ? "" : ", the 2D under the 3D is gone");
            fail(why);
        } else {
            kprintf("ok  ARM %lu us, GPU %lu us (%lu triangles in %lu jobs%s; bin %lu, render %lu), "
                    "%d.%d%% differ\n", us[0], us[1], st.tris, st.jobs, st.zjobs ? ", depth kept" : "",
                    st.bin_us, st.render_us, permille / 10, permille % 10);
        }
        /* the two pictures, ARM left and GPU right, until a key or 10 s */
        if (fb->depth == 32 && fb->width >= W && fb->height >= H) {
            kprintf("  the picture: ARM on the left, GPU on the right (a key or 10 s)\n");
            timer_delay_ms(40);
            fb_fill_rect(fb, 0, 0, W, H, fb_color(fb, 0, 0, 0));
            for (int y = 0; y < H / 2; y++)
                for (int x = 0; x < W; x++) {
                    const uint16_t *src = pg[x >= W / 2] + (y * 2) * W + (x % (W / 2)) * 2;
                    uint32_t c = g16_to_rgb24(*src);
                    fb_putpixel(fb, (uint32_t)x, (uint32_t)(y + H / 4), fb_color(fb, (uint8_t)(c >> 16),
                                (uint8_t)(c >> 8), (uint8_t)c));
                }
            uint32_t t0 = timer_ticks();
            input_flush();
            while (timer_ticks() - t0 < 10000000u && input_key() < 0)
                ;
            console_suspend(1);
            console_suspend(0);
        }
    }
    scene_free();
    free(pg[0]);
    free(pg[1]);
    return err ? -1 : 0;
}

/* 12: the textures in rows and in T-format, as the probe learned it: a
 * floor with a 256x256 texture from under the camera to far away (the
 * texels spread out: the TMU's cache matters), the same picture both ways
 * and the GPU's time of each. A difference turns the tiles off. */
static int step_tiles(void)
{
    step("12 textures in tiles (T-format)");
    if (gpu3d_init() != 0) {
        fail(gpu3d_status());
        return -1;
    }
    if (!gpu3d_tiles()) {
        kprintf("skipped: the probe found no T-format (textures stay in rows)\n");
        return 0;
    }
    gpu3d_drop();                           /* no depth kept from step 11 */
    g16_sheet_t sheet;
    r3d_mesh_t floor_m;
    memset(&sheet, 0, sizeof sheet);
    memset(&floor_m, 0, sizeof floor_m);
    uint16_t *pg[2] = { aligned_alloc(64, W * H * 2), aligned_alloc(64, W * H * 2) };
    int err = !pg[0] || !pg[1] || g16_sheet_alloc(&sheet, 256, 256) != 0 || r3d_mesh_alloc(&floor_m, 4, 2) != 0 ||
              r3d_mesh_alloc_uv(&floor_m) != 0;
    uint32_t us[2] = { 0, 0 };
    if (!err) {
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++)
                g16_sheet_set(&sheet, x, y, g16_rgb((uint32_t)(x ^ y), (uint32_t)(x * 3 + y) & 255,
                                                    (uint32_t)(y * 5) & 255), 1);
        for (int cy = 0; cy < 32; cy++)
            for (int cx = 0; cx < 32; cx++)
                g16_sheet_update_cell(&sheet, cx, cy);
        floor_m.verts[0] = (v3_t){ -1, -1, 0 }; floor_m.verts[1] = (v3_t){ 1, -1, 0 };
        floor_m.verts[2] = (v3_t){ 1, 1, 0 };   floor_m.verts[3] = (v3_t){ -1, 1, 0 };
        static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
        memcpy(floor_m.faces, f, sizeof f);
        static const float uv[12] = { 0, 255.5f, 255.5f, 0, 255.5f, 255.5f,   0, 255.5f, 0, 0, 255.5f, 0 };
        memcpy(floor_m.uv, uv, sizeof uv);
        floor_m.colors[0] = floor_m.colors[1] = R3D_TEXTURED;
        floor_m.tex = &sheet;
        r3d_mesh_normals(&floor_m);
    }
    for (int pass = 0; pass < 2 && !err; pass++) {
        g16_t g;
        r3d_t r;
        g16_target(&g, pg[pass], W, W, H, &font_console_8x16);
        if (r3d_init(&r, &g) != 0) {
            err = 1;
            break;
        }
        r.backend = gpu3d_backend();
        gpu3d_tiled_textures(pass);
        gpu3d_stats_t st;
        for (int k = 0; k < 6; k++) {       /* the first: the texture made */
            if (k == 1)
                gpu3d_take_stats(&st);
            g16_cls(&g, g16_rgb(30, 20, 50));
            r3d_zclear(&r);
            r3d_camera(&r, 0, 1.0f, -1.0f, 0, -0.35f, 70);
            r3d_light(&r, 0, 1, 0, 1);
            r3d_draw_flags(&r, &floor_m, (v3_t){ 0, 0, 6 }, 1.5707963f, 0, 0, 7, R3D_UNLIT);
            if (gpu3d_flush(&g, 0) != 0)
                err = 2;
        }
        gpu3d_take_stats(&st);
        us[pass] = st.render_us / 5;
        r3d_free(&r);
    }
    gpu3d_tiled_textures(1);
    int differ = 0;
    if (!err)
        for (int i = 0; i < W * H; i++)
            differ += pg[0][i] != pg[1][i];
    if (err) {
        fail(err == 2 ? gpu3d_status() : "no memory for the texture");
    } else if (differ) {
        gpu3d_tiled_textures(0);            /* the games keep the rows */
        char why[80];
        ksnprintf(why, sizeof why, "%d pixels differ: textures back in rows", differ);
        fail(why);
    } else {
        const uint32_t x10 = us[1] ? us[0] * 10 / us[1] : 0;
        kprintf("ok  render in rows %lu us, in tiles %lu us (%lu.%lux), the same picture\n", us[0], us[1],
                x10 / 10, x10 % 10);
    }
    r3d_mesh_free(&floor_m);
    g16_sheet_free(&sheet);
    free(pg[0]);
    free(pg[1]);
    return err || differ ? -1 : 0;
}

/* 13: anti-aliasing (MSAA 4x): the scene of step 10 on a cleared page,
 * by the GPU without and with MSAA: the GPU's time of each, how much the
 * smoothing changed (edges only: the mean of every 8x8 block stays), and
 * the middle of both pictures twice as big, without on the left */
static int step_msaa(framebuffer_t *fb)
{
    step("13 anti-aliasing (MSAA 4x)");
    if (gpu3d_init() != 0) {
        fail(gpu3d_status());
        return -1;
    }
    const int where = gpu3d_msaa();
    if (!where) {
        kprintf("skipped: no MSAA on this GPU\n");
        return 0;
    }
    gpu3d_drop();                           /* step 11 taught it to keep the depth: no MSAA then */
    uint16_t *pg[2] = { aligned_alloc(64, W * H * 2), aligned_alloc(64, W * H * 2) };
    uint32_t us[2] = { 0, 0 }, ms_jobs = 0;
    int err = !pg[0] || !pg[1] || scene_init() != 0;
    for (int pass = 0; pass < 2 && !err; pass++) {
        g16_t g;
        r3d_t r;
        g16_target(&g, pg[pass], W, W, H, &font_console_8x16);
        if (r3d_init(&r, &g) != 0) {
            err = 1;
            break;
        }
        r.backend = gpu3d_backend();
        gpu3d_set_msaa(pass);
        gpu3d_stats_t st;
        scene_aa(&g, &r, 1);                /* textures made, caches warm */
        gpu3d_take_stats(&st);
        scene_aa(&g, &r, 1);
        gpu3d_take_stats(&st);
        if (gpu3d_failed())
            err = 2;
        us[pass] = st.bin_us + st.render_us;
        if (pass)
            ms_jobs = st.msjobs;
        r3d_free(&r);
    }
    gpu3d_set_msaa(0);
    if (err) {
        fail(err == 2 ? gpu3d_status() : "no memory for the scene");
    } else {
        int differ = 0, blocks = 0, moved = 0;
        for (int i = 0; i < W * H; i++)
            differ += pg[0][i] != pg[1][i];
        for (int by = 0; by + 8 <= H; by += 8)
            for (int bx = 0; bx + 8 <= W; bx += 8, blocks++) {
                int sum[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
                for (int y = by; y < by + 8; y++)
                    for (int x = bx; x < bx + 8; x++)
                        for (int p = 0; p < 2; p++) {
                            uint32_t c = g16_to_rgb24(pg[p][y * W + x]);
                            for (int k = 0; k < 3; k++)
                                sum[p][k] += (int)(c >> (8 * k) & 255);
                        }
                for (int k = 0; k < 3; k++)
                    if (sum[0][k] - sum[1][k] > 16 * 64 || sum[1][k] - sum[0][k] > 16 * 64) {
                        moved++;
                        break;
                    }
            }
        const int permille = (int)((int64_t)differ * 1000 / (W * H));
        if (!ms_jobs || !differ || moved * 100 > blocks) {
            char why[100];
            ksnprintf(why, sizeof why, "%lu MSAA jobs, %d.%d%% of the pixels changed, %d of %d blocks moved",
                      ms_jobs, permille / 10, permille % 10, moved, blocks);
            fail(why);
        } else {
            kprintf("ok  GPU without %lu us, with %lu us; %d.%d%% of the pixels smoothed (MSAA %s)\n", us[0],
                    us[1], permille / 10, permille % 10, where == 2 ? "on any page" : "on cleared pages");
        }
        /* the middle of both pictures, twice as big, until a key or 10 s */
        if (fb->depth == 32 && fb->width >= W && fb->height >= H) {
            kprintf("  the picture: without anti-aliasing on the left, with on the right (2x, a key or 10 s)\n");
            timer_delay_ms(40);
            fb_fill_rect(fb, 0, 0, W, H, fb_color(fb, 0, 0, 0));
            for (int y = 0; y < H / 2; y++)
                for (int x = 0; x < W; x++) {
                    const int half = x >= W / 2, sx = W / 2 - W / 8 + (x % (W / 2)) / 2,
                              sy = H / 2 - H / 8 + y / 2;
                    uint32_t c = g16_to_rgb24(pg[half][sy * W + sx]);
                    fb_putpixel(fb, (uint32_t)x, (uint32_t)(y + H / 4),
                                fb_color(fb, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c));
                }
            uint32_t t0 = timer_ticks();
            input_flush();
            while (timer_ticks() - t0 < 10000000u && input_key() < 0)
                ;
            console_suspend(1);
            console_suspend(0);
        }
    }
    scene_free();
    free(pg[0]);
    free(pg[1]);
    return err ? -1 : 0;
}

/* the scene of step 14: "lit" models as a map has them (light baked at
 * the corners): a floor from under the camera (the GPU clips it at the
 * near plane), rows of cubes, unlit spheres; spheres lit by the sun and
 * the sky as heroes are (Gouraud, rim, highlights); fog past them, a lamp */
static void scene_vs(g16_t *g, r3d_t *r, int gpu)
{
    g16_cls(g, g16_rgb(30, 20, 50));
    r3d_zclear(r);
    r3d_camera(r, 0, 1.2f, -7, 0.1f, -0.15f, 60);
    r3d_light(r, -0.4f, 0.7f, -0.6f, 0.3f);
    r3d_fog(r, 0xC0A080, 40, 90);
    r3d_lamp_rgb(r, 0, -1.5f, 0, -2, 3, 0.8f, 0xFF8040);
    r3d_draw_flags(r, &sc_floor, (v3_t){ 0, -1.2f, 2 }, 1.5707963f, 0, 0, 12, 0);
    for (int i = 0; i < 40; i++)
        r3d_draw_flags(r, &sc_cube, (v3_t){ -4.5f + 1.0f * (float)(i % 10), -0.8f + 0.9f * (float)(i / 10),
                                            (float)(i / 10) * 1.5f }, 0.3f * (float)i, 0.5f, 0, 0.35f, 0);
    for (int i = 0; i < 12; i++)
        r3d_draw_flags(r, &sc_sphere, (v3_t){ -3.3f + 0.6f * (float)i, 1.8f, 3 }, 0, 0.3f * (float)i, 0, 0.25f,
                       R3D_UNLIT);
    r3d_sky(r, 0xFFF0D0, 0x90B0FF, 0x806040);
    r3d_shine(r, 0.6f, 16, 0.4f);
    for (int i = 0; i < 8; i++)
        r3d_draw_flags(r, &sc_sphere, (v3_t){ -3.2f + 0.9f * (float)i, 1.0f, 1 }, 0, 0.4f * (float)i, 0, 0.35f,
                       i & 1 ? R3D_SMOOTH : 0);
    r3d_shine(r, 0.6f, 16, 0);              /* as r3d_init */
    r3d_sky(r, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
    r3d_lamp(r, 0, 0, 0, 0, 0, 0);
    r3d_fog(r, 0, 0, 0);
    if (gpu)
        gpu3d_flush(g, 0);
}

/* 14 (M36): the scene above by the GPU with the corners placed by the ARM
 * (left) and by the GPU's vertex shader (right): the same picture, and
 * the ARM's time of each */
static int step_vshader(framebuffer_t *fb)
{
    step("14 vertex shader (the GPU places the corners)");
    if (gpu3d_init() != 0) {
        fail(gpu3d_status());
        return -1;
    }
    if (!gpu3d_vshader()) {
        kprintf("skipped: %s\n", gpu3d_status());
        return 0;
    }
    gpu3d_drop();
    const int was = gpu3d_vshader_on();
    uint16_t *pg[2] = { aligned_alloc(64, W * H * 2), aligned_alloc(64, W * H * 2) };
    uint32_t us[2] = { 0, 0 }, glm = 0, gpu_us[2] = { 0, 0 };
    int err = !pg[0] || !pg[1] || scene_init() != 0;
    r3d_mesh_t *lit[2] = { &sc_floor, &sc_cube };
    for (int k = 0; k < 2 && !err; k++) {
        lit[k]->clight = malloc((size_t)lit[k]->nfaces * 9);
        if (!lit[k]->clight) {
            err = 1;
            break;
        }
        for (int i = 0; i < lit[k]->nfaces * 9; i++)
            lit[k]->clight[i] = (uint8_t)(k ? 70 + (i * 37) % 140 : 150);
        r3d_mesh_normals(lit[k]);
    }
    for (int pass = 0; pass < 2 && !err; pass++) {
        g16_t g;
        r3d_t r;
        g16_target(&g, pg[pass], W, W, H, &font_console_8x16);
        if (r3d_init(&r, &g) != 0) {
            err = 1;
            break;
        }
        r.backend = gpu3d_backend();
        gpu3d_set_vshader(pass ? 2 : 0);    /* every model */
        gpu3d_stats_t st;
        scene_vs(&g, &r, 1);                /* corners made, caches warm */
        gpu3d_take_stats(&st);
        uint32_t t0 = timer_ticks();
        scene_vs(&g, &r, 1);
        us[pass] = timer_ticks() - t0;
        gpu3d_take_stats(&st);
        gpu_us[pass] = st.bin_us + st.render_us;
        if (pass)
            glm = st.glmeshes;
        if (gpu3d_failed())
            err = 2;
        r3d_free(&r);
    }
    gpu3d_set_vshader(was);
    if (err) {
        fail(err == 2 ? gpu3d_status() : "no memory for the scene");
    } else {
        int differ = 0;
        for (int i = 0; i < W * H; i++)
            differ += differs(pg[0][i], pg[1][i]);
        const int permille = (int)((int64_t)differ * 1000 / (W * H));
        if (permille > 10 || !glm) {
            char why[100];
            ksnprintf(why, sizeof why, "%d.%d%% of the pixels differ, %lu meshes by the vertex shader",
                      permille / 10, permille % 10, glm);
            fail(why);
        } else {
            kprintf("ok  ARM %lu us without, %lu us with (%lu meshes; GPU %lu / %lu us), %d.%d%% differ\n",
                    us[0], us[1], glm, gpu_us[0], gpu_us[1], permille / 10, permille % 10);
        }
        if (fb->depth == 32 && fb->width >= W && fb->height >= H) {
            kprintf("  the picture: corners by the ARM on the left, by the GPU on the right (a key or 10 s)\n");
            timer_delay_ms(40);
            fb_fill_rect(fb, 0, 0, W, H, fb_color(fb, 0, 0, 0));
            for (int y = 0; y < H / 2; y++)
                for (int x = 0; x < W; x++) {
                    const uint16_t *src = pg[x >= W / 2] + (y * 2) * W + (x % (W / 2)) * 2;
                    uint32_t c = g16_to_rgb24(*src);
                    fb_putpixel(fb, (uint32_t)x, (uint32_t)(y + H / 4), fb_color(fb, (uint8_t)(c >> 16),
                                (uint8_t)(c >> 8), (uint8_t)c));
                }
            uint32_t t0 = timer_ticks();
            input_flush();
            while (timer_ticks() - t0 < 10000000u && input_key() < 0)
                ;
            console_suspend(1);
            console_suspend(0);
        }
    }
    scene_free();
    free(pg[0]);
    free(pg[1]);
    return err ? -1 : 0;
}

void gpu_test(framebuffer_t *fb)
{
    failed = 0;
    kprintf("GPU test: the 3D unit (V3D), step by step (if the Pi freezes, the last line is the culprit)\n");

    step("1 power on the 3D unit (mailbox)");
    if (v3d_init() != 0) {
        fail(v3d_status());
        kprintf("GPU test \x1b[91mfailed\x1b[0m\n");
        return;
    }
    kprintf("ok  V3D clock %lu MHz\n", prop_clock_rate(CLOCK_V3D) / 1000000);

    step("2 identity");
    uint32_t id1 = v3d_ident(1);
    kprintf("ok  rev %lu, %lu slices x %lu QPUs, %lu TMUs per slice, VPM %lu KiB\n", id1 & 15,
            id1 >> 4 & 15, id1 >> 8 & 15, id1 >> 12 & 15, (id1 >> 28 & 15) ? (id1 >> 28 & 15) : 16);

    if (mem_init() != 0) {
        kprintf("GPU test: no memory for the V3D\n");
        return;
    }
    int ok = step_clear() == 0 && step_triangle() == 0 && step_depth() == 0 && step_speed_small() == 0 &&
             step_speed_fill() == 0 && step_texture() == 0 && step_screen(fb) == 0;
    free(m.block);
    m.block = NULL;
    if (ok && step_compare(fb, "10 the 3D of the games on the GPU", scene_draw) == 0 &&
        step_compare(fb, "11 depth kept across 2D (3D, 2D, 3D)", scene_split) == 0 &&
        step_tiles() == 0 && step_msaa(fb) == 0)
        step_vshader(fb);
    if (gpu3d_ready())
        gpu3d_drop();                       /* the games start from a clean state */
    kprintf(failed ? "GPU test \x1b[91mfailed\x1b[0m: a photo of these lines helps\n" : "GPU test passed\n");
}
