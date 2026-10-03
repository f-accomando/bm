#include "stress.h"
#include "gfx16.h"
#include "r3d.h"
#include "runtime.h"
#include "kernel/irq.h"
#include "kernel/tick.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gpu/gpu3d.h"
#include "lib/printf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 360
#define FRAMES_PER_STEP 20
#define LIMIT_MS        40.0f
#define MS60            16.667f
#define MS30            33.333f

typedef struct {
    const char *name;
    const char *unit;
    int start_n;
    int max_n;
    void (*setup)(void);
    void (*frame)(int n, int f);    /* draws one frame with n items */
    void (*teardown)(void);
    int gpu;                        /* the 3D drawn by the GPU (gpu3d): 1, 2 with MSAA 4x, 3 with the
                                     * vertex shader placing every model (M36) */
} test_t;

static g16_t g;
static g16_sheet_t sheet;
static r3d_t r3d;
static r3d_mesh_t sphere;
static uint32_t tris_last;
static unsigned spheres_flags;     /* r3d flags of the spheres test */

/* ---------------------------------------------------------------- scenes */

static void sheet_setup(void)
{
    g16_sheet_alloc(&sheet, 64, 32);
    /* 16x16 ball at (0,0) and 32x32 ball at (16,0): transparent corners */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 64; x++) {
            int in16 = x < 16 && y < 16 && (x - 7.5f) * (x - 7.5f) + (y - 7.5f) * (y - 7.5f) < 60;
            int in32 = x >= 16 && x < 48 && (x - 31.5f) * (x - 31.5f) + (y - 15.5f) * (y - 15.5f) < 240;
            g16_sheet_set(&sheet, x, y, g16_rgb((uint32_t)(x * 4), (uint32_t)(y * 8), 200), in16 || in32);
        }
}

static void sheet_teardown(void) { g16_sheet_free(&sheet); }

static void sprites(int n, int f, int size)
{
    g16_cls(&g, g16_rgb(10, 10, 30));
    for (int i = 0; i < n; i++) {
        int x = (i * 97 + f * (1 + i % 5)) % (W + size) - size;
        int y = (i * 61 + f * (1 + i % 3)) % (H + size) - size;
        if (size == 16) g16_sspr(&g, &sheet, 0, 0, 16, 16, x, y, i & 1, 0);
        else            g16_sspr(&g, &sheet, 16, 0, 32, 32, x, y, i & 1, 0);
    }
}

static void spr16(int n, int f) { sprites(n, f, 16); }
static void spr32(int n, int f) { sprites(n, f, 32); }

/* triangles about 20 px across (area ~ 170 px) */
static void tris2d(int n, int f)
{
    g16_cls(&g, g16_rgb(10, 10, 30));
    for (int i = 0; i < n; i++) {
        int x = (i * 131 + f * 3) % (W - 20), y = (i * 71 + f * 2) % (H - 20);
        g16_tri(&g, x, y, x + 20, y + 4, x + 6, y + 18,
                g16_rgb((uint32_t)(i * 37), (uint32_t)(i * 91), (uint32_t)(i * 53)));
    }
}

static void sphere_setup(void)
{
    r3d_init(&r3d, &g);
    r3d_mesh_sphere(&sphere, 6, 8, 0x4080FF, 0xFFC040);       /* 96 faces */
}

static void sphere_teardown(void)
{
    r3d_mesh_free(&sphere);
    r3d_free(&r3d);
}

/* n spheres of 96 triangles on a grid in front of the camera */
/* a page cleared to one colour: with the GPU, its job starts from that
 * colour instead of loading the page, as in a game after cls() */
static void cls3d(void)
{
    g16_cls(&g, g16_rgb(10, 10, 30));
    if (r3d.backend)
        gpu3d_page(1, g16_rgb(10, 10, 30));
}

static void spheres3d(int n, int f)
{
    cls3d();
    r3d_zclear(&r3d);
    r3d.g = &g;
    r3d_camera(&r3d, 0, 0, -8, 0, 0, 60);
    int side = (int)ceilf(sqrtf((float)n));
    float step = 9.0f / (side > 1 ? side : 1);
    for (int i = 0; i < n; i++) {
        float x = -4.5f + step * (i % side + 0.5f), y = 2.6f - step * 0.56f * (i / side + 0.5f);
        r3d_draw_flags(&r3d, &sphere, (v3_t){ x, y, (float)(i % 3) }, f * 0.03f + i, f * 0.05f, 0,
                       step * 0.45f, spheres_flags);
    }
    tris_last = r3d.tris_drawn;
}

static void spheres3d_smooth(int n, int f)
{
    spheres_flags = R3D_SMOOTH;
    spheres3d(n, f);
    spheres_flags = 0;
}

/* the spheres textured with a 32x32 checker of the sheet (u, v per face
 * from the sphere's rings and segments) */
static void tex_sphere_setup(void)
{
    sheet_setup();
    for (int y = 0; y < 32; y++)
        for (int x = 48; x < 64; x++)
            g16_sheet_set(&sheet, x, y, ((x >> 2) ^ (y >> 2)) & 1 ? g16_rgb(240, 200, 60) : g16_rgb(40, 90, 200), 1);
    sphere_setup();
    r3d_mesh_alloc_uv(&sphere);
    for (int t = 0; t < sphere.nfaces; t++) {
        sphere.colors[t] = R3D_TEXTURED;
        for (int i = 0; i < 3; i++) {
            int v = sphere.faces[t * 3 + i];
            sphere.uv[t * 6 + i * 2] = 48 + (v % 8) * 2.0f;
            sphere.uv[t * 6 + i * 2 + 1] = (v / 8) * 5.0f;
        }
    }
    sphere.tex = &sheet;
}

static void tex_sphere_teardown(void)
{
    sphere_teardown();
    sheet_teardown();
}

/* Quads of 320x180 pixels (a quarter of the screen, a whole 320x180
 * screen of pixels), one per quadrant in turn, each nearer than the one
 * before: every pixel passes the depth test and is written. The slope of
 * these rows is the cost of one pixel, without the per-triangle work of
 * the spheres. The texture is 256x256, about one texel every 1.25 pixels
 * across the quad, larger than the data cache like a real scene's. */
#define QUAD_PX (320 * 180)

static r3d_mesh_t quad;
static unsigned quad_flags;

static void quad_mesh(int textured)
{
    r3d_init(&r3d, &g);
    r3d_mesh_alloc(&quad, 4, 2);
    quad.verts[0] = (v3_t){ -1, -0.5625f, 0 }; quad.verts[1] = (v3_t){ 1, -0.5625f, 0 };
    quad.verts[2] = (v3_t){ 1, 0.5625f, 0 };   quad.verts[3] = (v3_t){ -1, 0.5625f, 0 };
    static const uint16_t f[6] = { 0, 2, 1, 0, 3, 2 };
    memcpy(quad.faces, f, sizeof f);
    quad.colors[0] = quad.colors[1] = 0x80C0FF;
    if (textured) {
        g16_sheet_alloc(&sheet, 256, 256);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++)
                g16_sheet_set(&sheet, x, y, g16_rgb((uint32_t)(x ^ y), (uint32_t)(x * 3 + y) & 255,
                                                     (uint32_t)(y * 5) & 255), 1);
        r3d_mesh_alloc_uv(&quad);
        static const float uv[12] = { 0, 256, 256, 0, 256, 256,   0, 256, 0, 0, 256, 0 };
        memcpy(quad.uv, uv, sizeof uv);
        quad.colors[0] = quad.colors[1] = R3D_TEXTURED;
        quad.tex = &sheet;
    }
    r3d_mesh_normals(&quad);
}

static void quad_flat(void)     { quad_flags = 0; quad_mesh(0); }
static void quad_noz(void)      { quad_flags = R3D_NOZ; quad_mesh(0); }
static void quad_smooth(void)   { quad_flags = R3D_SMOOTH; quad_mesh(0); }
static void quad_tex(void)      { quad_flags = 0; quad_mesh(1); }

static void quad_teardown(void)
{
    if (quad.tex)
        g16_sheet_free(&sheet);
    r3d_mesh_free(&quad);
    r3d_free(&r3d);
}

static void quads(int n, int f)
{
    (void)f;
    cls3d();
    r3d_zclear(&r3d);
    r3d_camera(&r3d, 0, 0, 0, 0, 0, 60);
    r3d_light(&r3d, 0.3f, 0.4f, -1, 0.3f);
    const float k = 160.0f / r3d.focal;             /* half a quadrant per unit of depth */
    /* each nearer than the last by more than a step of the depth buffer:
     * 16 bits for the ARM (400 quads), 24 for the GPU (2000) */
    const float step = r3d.backend ? 0.9985f : 0.985f;
    float d = 40;
    for (int i = 0; i < n; i++, d *= step) {
        float x = (i & 1) ? k * d : -k * d, y = (i & 2) ? -0.5625f * k * d : 0.5625f * k * d;
        r3d_draw_flags(&r3d, &quad, (v3_t){ x, y, d }, 0, 0, 0, k * d, quad_flags);
    }
    tris_last = r3d.tris_drawn;
}

static const test_t tests[] = {
    { "sprites 16x16 (C)", "spr",  16, 60000, sheet_setup,  spr16,     sheet_teardown, 0 },
    { "sprites 32x32 (C)", "spr",  16, 30000, sheet_setup,  spr32,     sheet_teardown, 0 },
    { "triangles 2D ~170px", "tri", 16, 60000, NULL,        tris2d,    NULL, 0 },
    { "3D spheres 96 (C)", "obj",   1,  4000, sphere_setup, spheres3d, sphere_teardown, 0 },
    { "3D smooth (Gouraud)", "obj", 1,  4000, sphere_setup, spheres3d_smooth, sphere_teardown, 0 },
    { "3D textured", "obj",         1,  4000, tex_sphere_setup, spheres3d, tex_sphere_teardown, 0 },
    { "quad 320x180 flat", "q",     1,   400, quad_flat,    quads,     quad_teardown, 0 },
    { "quad 320x180 no z", "q",     1,   400, quad_noz,     quads,     quad_teardown, 0 },
    { "quad 320x180 Gouraud", "q",  1,   400, quad_smooth,  quads,     quad_teardown, 0 },
    { "quad 320x180 texture", "q",  1,   400, quad_tex,     quads,     quad_teardown, 0 },
    /* the same scenes with the GPU (M33): the ARM still transforms, lights
     * and clips; the time includes the GPU's job */
    { "GPU spheres 96", "obj",      1,  8000, sphere_setup, spheres3d, sphere_teardown, 1 },
    { "GPU smooth (Gouraud)", "obj", 1, 8000, sphere_setup, spheres3d_smooth, sphere_teardown, 1 },
    { "GPU textured", "obj",        1,  8000, tex_sphere_setup, spheres3d, tex_sphere_teardown, 1 },
    { "GPU quad flat", "q",         1,  2000, quad_flat,    quads,     quad_teardown, 1 },
    { "GPU quad Gouraud", "q",      1,  2000, quad_smooth,  quads,     quad_teardown, 1 },
    { "GPU quad texture", "q",      1,  2000, quad_tex,     quads,     quad_teardown, 1 },
    /* anti-aliasing (MSAA 4x), where the GPU can */
    { "GPU spheres AA 4x", "obj",   1,  8000, sphere_setup, spheres3d, sphere_teardown, 2 },
    { "GPU quad AA 4x", "q",        1,  2000, quad_flat,    quads,     quad_teardown, 2 },
    /* the vertex shader (M36): the ARM sends matrices and lights only */
    { "GPU+VS spheres 96", "obj",   1, 16000, sphere_setup, spheres3d, sphere_teardown, 3 },
    { "GPU+VS smooth", "obj",       1, 16000, sphere_setup, spheres3d_smooth, sphere_teardown, 3 },
};
#define NTESTS (int)(sizeof tests / sizeof *tests)

/* ---------------------------------------------------------------- runner
 * Per-step details go to the serial port only (uart_puts), the summary
 * table to the console. */

typedef struct { int n; float ms; float tris; } sample_t;

typedef struct { float n, tris; int state; } thr_t;    /* state: 0 ok, -1 below start, -2 above max */

static thr_t threshold(const sample_t *s, int count, float limit)
{
    if (count == 0 || s[0].ms > limit)
        return (thr_t){ 0, 0, -1 };         /* even the lightest load is too slow */
    for (int i = 1; i < count; i++)
        if (s[i].ms > limit) {
            const sample_t *a = &s[i - 1], *b = &s[i];
            float k = (limit - a->ms) / (b->ms - a->ms);
            return (thr_t){ a->n + k * (b->n - a->n), a->tris + k * (b->tris - a->tris), 0 };
        }
    return (thr_t){ 0, 0, -2 };             /* never reached within max_n */
}

static void print_threshold(thr_t t, const test_t *T)
{
    char buf[24];
    if (t.state == -1) ksnprintf(buf, sizeof buf, "<%d", T->start_n);
    else if (t.state == -2) ksnprintf(buf, sizeof buf, ">%d", T->max_n);
    else if (T->unit[0] == 'o') ksnprintf(buf, sizeof buf, "%d (%d tri)", (int)t.n, (int)t.tris);
    else ksnprintf(buf, sizeof buf, "%d", (int)t.n);
    kprintf("%16s", buf);
}

static void overlay(const char *name, int n, float ms)
{
    char line[80];
    int ms100 = (int)(ms * 100);
    ksnprintf(line, sizeof line, "stress: %s  n=%d  %d.%02d ms", name, n, ms100 / 100, ms100 % 100);
    g16_rectfill(&g, 0, 0, W, 16, 0);
    g16_text(&g, 0, 0, line, 0xFFFF);
}

static const char *irq_name(int irq)
{
    switch (irq) {
    case IRQ_TIMER1: return "timer";
    case IRQ_USB:    return "USB";
    case IRQ_AUX:    return "mini UART";
    case IRQ_UART:   return "BT UART";
    default:         return irq >= 16 && irq < 29 ? "DMA" : "?";
    }
}

/* Lines about the machine: clocks (the core clock drives the L2 cache and
 * the memory bus), temperature, the firmware's throttling flags
 * (under-voltage, capped frequency...), the time of a fixed CPU-only loop
 * and the share of time spent in interrupt handlers, with the two busiest
 * ones: what the drawing does not get. */
static void machine_line(const char *when)
{
    uint32_t temp[2] = { 0, 0 }, thr[1] = { 0xFFFF };
    prop_query(PROP_GET_TEMPERATURE, temp, 2);
    int have_thr = prop_query(PROP_GET_THROTTLED, thr, 1) == 0;
    volatile uint32_t acc = 1;
    uint32_t t0 = timer_ticks();
    for (uint32_t i = 0; i < 2000000; i++)
        acc = acc * 1664525u + 1013904223u;
    uint32_t loop = timer_ticks() - t0;
    char th[16];
    if (have_thr) ksnprintf(th, sizeof th, "%05lx", thr[0]);
    else ksnprintf(th, sizeof th, "n/a");
    kprintf("%s: ARM %lu MHz, core %lu (max %lu), V3D %lu, SDRAM %lu MHz, %lu.%lu C\n",
            when, prop_clock_rate(CLOCK_ARM) / 1000000, prop_clock_rate(CLOCK_CORE) / 1000000,
            prop_clock_max(CLOCK_CORE) / 1000000, prop_clock_rate(CLOCK_V3D) / 1000000,
            prop_clock_rate(CLOCK_SDRAM) / 1000000, temp[1] / 1000, temp[1] / 100 % 10);

    /* interrupts over half a second */
    static uint32_t before[64];
    for (int i = 0; i < 64; i++)
        before[i] = irq_busy_us(i);
    uint32_t n0 = irq_count();
    t0 = timer_ticks();
    timer_delay_ms(500);
    uint32_t span = timer_ticks() - t0, n = irq_count() - n0, busy = 0;
    int top[2] = { -1, -1 };
    uint32_t top_us[2] = { 0, 0 };
    for (int i = 0; i < 64; i++) {
        uint32_t d = irq_busy_us(i) - before[i];
        busy += d;
        if (d > top_us[0]) {
            top[1] = top[0]; top_us[1] = top_us[0];
            top[0] = i; top_us[0] = d;
        } else if (d > top_us[1]) {
            top[1] = i; top_us[1] = d;
        }
    }
    uint32_t pm = (uint32_t)((uint64_t)busy * 1000 / (span ? span : 1));   /* per mille */
    kprintf("  throttled %s, loop %lu.%02lu ms, irq %lu.%lu%% (%lu/s", th,
            loop / 1000, loop / 10 % 100, pm / 10, pm % 10, n * 2);
    for (int k = 0; k < 2; k++)
        if (top[k] >= 0) {
            uint32_t tp = (uint32_t)((uint64_t)top_us[k] * 1000 / (span ? span : 1));
            kprintf("%s %s %lu.%lu%%", k ? "," : ";", irq_name(top[k]), tp / 10, tp % 10);
        }
    kprintf(")\n");
}

void bm_stress_settle(uint32_t ms)
{
    uint32_t shown = 0;
    while (tick_ms() < ms) {
        uint32_t left = (ms - tick_ms() + 999) / 1000;
        if (left != shown) {
            kprintf("\rstress: waiting %2lu s for the boot to settle (WiFi, Bluetooth)", left);
            shown = left;
        }
        timer_delay_ms(20);
    }
    if (shown)
        kprintf("\n");
}

void bm_stress_run(framebuffer_t *fb)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    static sample_t samples[64];
    thr_t results[NTESTS][2];
    float per_item[NTESTS];

    int skip[NTESTS];
    memset(skip, 0, sizeof skip);

    kprintf("stress test: 640x360 RGB565, %d frames per step, draw + copy to screen\n", FRAMES_PER_STEP);
    machine_line("before");
    if (bm_video_enter(fb, W, H, &g) != 0) {
        bm_video_leave(fb, con_w, con_h);
        kprintf("stress: cannot set the video mode\n");
        return;
    }
    const int gpu = gpu3d_init() == 0;      /* after the video mode: its pages are known */

    for (size_t t = 0; t < sizeof tests / sizeof *tests; t++) {
        const test_t *T = &tests[t];
        int count = 0;
        if (T->gpu && !gpu3d_ready()) {
            skip[t] = 1;
            continue;
        }
        if (T->setup) T->setup();
        if (T->gpu) {
            r3d.backend = gpu3d_backend();
            gpu3d_drop();                   /* each row from a clean state */
            gpu3d_set_msaa(T->gpu == 2);
            gpu3d_set_vshader(T->gpu == 3 ? 2 : 0);
        }
        if (T->gpu == 2 && !gpu3d_msaa()) {
            if (T->teardown) T->teardown();
            skip[t] = 3;                    /* no MSAA on this GPU */
            continue;
        }
        if (T->gpu == 3 && gpu3d_vshader() < 2) {
            if (T->teardown) T->teardown();
            skip[t] = 4;                    /* no vertex shader for lit models on this GPU */
            continue;
        }
        uart_puts("\n");
        uart_puts(T->name);
        uart_puts(":\n");
        for (int n = T->start_n; n <= T->max_n && count < 64; n = n * 3 / 2 > n ? n * 3 / 2 : n + 1) {
            uint32_t total = 0;
            for (int f = 0; f < FRAMES_PER_STEP; f++) {
                uint32_t t0 = timer_ticks();
                T->frame(n, f);
                if (T->gpu)
                    gpu3d_flush(&g, 0);
                uint32_t draw = timer_ticks() - t0;
                overlay(T->name, n, (total + draw) / 1000.0f / (f + 1));
                total += draw + bm_video_present(fb, &g);  /* the copy is part of the frame */
            }
            float ms = total / 1000.0f / FRAMES_PER_STEP;
            samples[count++] = (sample_t){ n, ms, (float)tris_last };
            char line[96];
            int ms100 = (int)(ms * 100);
            ksnprintf(line, sizeof line, "  n=%6d  %4d.%02d ms\n", n, ms100 / 100, ms100 % 100);
            uart_puts(line);
            if (T->unit[0] == 'o') {
                ksnprintf(line, sizeof line, "           %lu triangles drawn\n", tris_last);
                uart_puts(line);
            }
            if (ms > LIMIT_MS || (T->gpu && !gpu3d_ready()))
                break;
        }
        if (T->gpu && !gpu3d_ready())
            skip[t] = 2;                    /* failed during the test */
        if (T->teardown) T->teardown();
        tris_last = 0;
        results[t][0] = threshold(samples, count, MS60);
        results[t][1] = threshold(samples, count, MS30);
        /* cost per item: slope between the first and last sample */
        per_item[t] = count > 1 ? (samples[count - 1].ms - samples[0].ms) * 1000.0f /
                                  (samples[count - 1].n - samples[0].n) : 0;
    }
    if (gpu) {
        gpu3d_set_msaa(0);
        gpu3d_set_vshader(0);
    }
    bm_video_leave(fb, con_w, con_h);
    machine_line("after C part");

    kprintf("\x1b[1m%-24s%16s%16s%12s\x1b[0m\n", "test (max per frame)", "60 fps", "30 fps", "us/item");
    for (size_t t = 0; t < sizeof tests / sizeof *tests; t++) {
        if (skip[t] == 1)
            continue;
        kprintf("%-24s", tests[t].name);
        if (skip[t] == 2) {
            kprintf("  the GPU failed: %s\n", gpu3d_status());
            continue;
        }
        if (skip[t] == 3) {
            kprintf("  no MSAA on this GPU\n");
            continue;
        }
        if (skip[t] == 4) {
            kprintf("  no vertex shader for lit models on this GPU\n");
            continue;
        }
        print_threshold(results[t][0], &tests[t]);
        print_threshold(results[t][1], &tests[t]);
        int us100 = (int)(per_item[t] * 100);
        kprintf("%7d.%02d %s", us100 / 100, us100 % 100, tests[t].unit);
        if (tests[t].unit[0] == 'q')        /* one quad = QUAD_PX pixels */
            kprintf(" %3d ns/px", (int)(per_item[t] * 1000.0f / QUAD_PX + 0.5f));
        kprintf("\n");
    }
    if (!gpu)
        kprintf("GPU rows: none (%s)\n", gpu3d_status());
}
