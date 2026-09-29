#include "stress.h"
#include "gfx16.h"
#include "r3d.h"
#include "runtime.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
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
static void spheres3d(int n, int f)
{
    g16_cls(&g, g16_rgb(10, 10, 30));
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

static const test_t tests[] = {
    { "sprites 16x16 (C)", "spr",  16, 60000, sheet_setup,  spr16,     sheet_teardown },
    { "sprites 32x32 (C)", "spr",  16, 30000, sheet_setup,  spr32,     sheet_teardown },
    { "triangles 2D ~170px", "tri", 16, 60000, NULL,        tris2d,    NULL },
    { "3D spheres 96 (C)", "obj",   1,  4000, sphere_setup, spheres3d, sphere_teardown },
    { "3D smooth (Gouraud)", "obj", 1,  4000, sphere_setup, spheres3d_smooth, sphere_teardown },
    { "3D textured", "obj",         1,  4000, tex_sphere_setup, spheres3d, tex_sphere_teardown },
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

void b33_stress_run(framebuffer_t *fb)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    static sample_t samples[64];
    thr_t results[NTESTS][2];
    float per_item[NTESTS];

    kprintf("stress test: 640x360 RGB565, %d frames per step, draw + copy to screen\n", FRAMES_PER_STEP);
    if (b33_video_enter(fb, W, H, &g) != 0) {
        b33_video_leave(fb, con_w, con_h);
        kprintf("stress: cannot set the video mode\n");
        return;
    }

    for (size_t t = 0; t < sizeof tests / sizeof *tests; t++) {
        const test_t *T = &tests[t];
        int count = 0;
        if (T->setup) T->setup();
        uart_puts("\n");
        uart_puts(T->name);
        uart_puts(":\n");
        for (int n = T->start_n; n <= T->max_n && count < 64; n = n * 3 / 2 > n ? n * 3 / 2 : n + 1) {
            uint32_t total = 0;
            for (int f = 0; f < FRAMES_PER_STEP; f++) {
                uint32_t t0 = timer_ticks();
                T->frame(n, f);
                uint32_t draw = timer_ticks() - t0;
                overlay(T->name, n, (total + draw) / 1000.0f / (f + 1));
                total += draw + b33_video_present(fb, &g);  /* the copy is part of the frame */
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
            if (ms > LIMIT_MS)
                break;
        }
        if (T->teardown) T->teardown();
        tris_last = 0;
        results[t][0] = threshold(samples, count, MS60);
        results[t][1] = threshold(samples, count, MS30);
        /* cost per item: slope between the first and last sample */
        per_item[t] = count > 1 ? (samples[count - 1].ms - samples[0].ms) * 1000.0f /
                                  (samples[count - 1].n - samples[0].n) : 0;
    }
    b33_video_leave(fb, con_w, con_h);

    kprintf("\x1b[1m%-24s%16s%16s%12s\x1b[0m\n", "test (max per frame)", "60 fps", "30 fps", "us/item");
    for (size_t t = 0; t < sizeof tests / sizeof *tests; t++) {
        kprintf("%-24s", tests[t].name);
        print_threshold(results[t][0], &tests[t]);
        print_threshold(results[t][1], &tests[t]);
        int us100 = (int)(per_item[t] * 100);
        kprintf("%7d.%02d %s\n", us100 / 100, us100 % 100, tests[t].unit);
    }
}
