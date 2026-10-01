#include "gputest.h"
#include "crumbs.h"
#include "input.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "gfx/console.h"
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
#define CODE_SIZE     1024
#define VERTS_SIZE    ((MAX_VERTS * sizeof(nv_vert_t) + 4095) & ~4095u)
#define TARGET_SIZE   (W * H * 2)
#define BLOCK_SIZE    (TSDA_SIZE + ALLOC_SIZE + OVERFLOW_SIZE + BCL_SIZE + RCL_SIZE + REC_SIZE + \
                       CODE_SIZE + VERTS_SIZE + TARGET_SIZE)

static struct {
    uint8_t *block;
    uint8_t *tsda, *alloc, *overflow, *bcl, *rcl, *rec;
    uint32_t *code;
    nv_vert_t *verts;
    uint16_t *target;               /* 640x360 RGB565, in RAM */
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
    memset(m.tsda, 0, TSDA_SIZE);
    memcpy(m.code, fs_colour, sizeof fs_colour);
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
 * fs_colour (3 varyings); cfg = CONFIGURATION_BITS (first byte, rest). */
static void bcl_build(int w, int h, uint8_t cfg8, uint16_t cfg16)
{
    const int tx = (w + V3D_TILE - 1) / V3D_TILE, ty = (h + V3D_TILE - 1) / V3D_TILE;
    /* shader record (NV): flags, vertex stride, uniforms, varyings, code,
     * uniforms address, vertices */
    uint8_t *r = m.rec;
    r[0] = 0;
    r[1] = sizeof(nv_vert_t);
    r[2] = 0;
    r[3] = 3;
    uint32_t a = v3d_bus(m.code);
    memcpy(r + 4, &a, 4);
    a = 0;
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

/* a picture drawn by the GPU straight into the console's page */
static int step_screen(framebuffer_t *fb)
{
    step("8 on the screen (RGBA8888)");
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
    if (step_clear() == 0 && step_triangle() == 0 && step_depth() == 0 && step_speed_small() == 0 &&
        step_speed_fill() == 0)
        step_screen(fb);
    free(m.block);
    m.block = NULL;
    kprintf(failed ? "GPU test \x1b[91mfailed\x1b[0m: a photo of these lines helps\n" : "GPU test passed\n");
}
