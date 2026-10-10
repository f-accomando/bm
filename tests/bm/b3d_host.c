/*
 * The 3D Bench (src/bm/b3d.c) on the PC: the V3D emulated
 * (tests/gpu/v3d_emu.c), quick mode (a few steps, one frame each), the
 * reports in DIR as on the SD card, every page of results as DIR/page-NN.ppm.
 *
 *   b3d_host DIR [--full] [-v]     B3D_EMU_SKIP=1: the V3D's jobs not run
 *   b3d_host --selftest            the ramp's arithmetic
 *
 * make test-b3d runs it twice: the second run must read the first's
 * report (its "last" bars and the comparison of the summary).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bm/b3d.h"
#include "bm/r3d.h"
#include "gpu/gpu3d.h"
#include "lib/printf.h"
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

int kvprintf(putc_fn out, void *ctx, const char *fmt, va_list ap)
{
    char b[1024];
    int n = vsnprintf(b, sizeof b, fmt, ap);
    for (int i = 0; i < n && i < (int)sizeof b - 1; i++)
        out(b[i], ctx);
    return n;
}

extern const font_t font_console_8x16;

static const char *dir;
static g16_t page;
static int verbose;

static uint32_t us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000000 + t.tv_nsec / 1000);
}

static int frames;                      /* --frames: every frame shown as DIR/frame-NNNN.ppm */
static void shown(int i);

static uint32_t present(void)
{
    static int k;
    if (frames)
        shown(10000 + k++);
    return 0;
}

static int key(void) { return B3D_KEY_BACK; }

/* --stop=N: the user stops the run at the Nth frame (Start+Select, PS) */
static int stop_at, stop_calls;
static int stop(void) { return ++stop_calls >= stop_at; }

static void log_line(const char *s)
{
    if (verbose)
        printf("%s\n", s);
}

static int last_number(void)
{
    int best = 0;
    for (int k = 1; k < 10000; k++) {
        char path[512];
        snprintf(path, sizeof path, "%s/3D%04d.TXT", dir, k);
        FILE *f = fopen(path, "r");
        if (!f)
            break;
        fclose(f);
        best = k;
    }
    return best;
}

static int save(const char *text, size_t len, char *name, size_t n)
{
    char path[512];
    const int k = last_number() + 1;
    snprintf(path, sizeof path, "%s/3D%04d.TXT", dir, k);
    FILE *f = fopen(path, "w");
    if (!f || fwrite(text, 1, len, f) != len) {
        if (f)
            fclose(f);
        return -1;
    }
    fclose(f);
    snprintf(name, n, "3D%04d.TXT", k);
    return 0;
}

static int load_last(char **text, char *name, size_t n)
{
    const int k = last_number();
    if (!k)
        return -1;
    char path[512];
    snprintf(path, sizeof path, "%s/3D%04d.TXT", dir, k);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)len + 1);
    if (!t || fread(t, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(t);
        return -1;
    }
    t[len] = 0;
    fclose(f);
    *text = t;
    snprintf(name, n, "3D%04d.TXT", k);
    return 0;
}

static void shown(int i)
{
    char path[512];
    if (i >= 10000)
        snprintf(path, sizeof path, "%s/frame-%04d.ppm", dir, i - 10000);
    else
        snprintf(path, sizeof path, "%s/page-%02d.ppm", dir, i + 1);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", page.w, page.h);
    for (int y = 0; y < page.h; y++)
        for (int x = 0; x < page.w; x++) {
            const uint16_t c = page.px[y * (int)page.stride + x];
            const uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2),
                                     (uint8_t)((c & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* --selftest: how the ramp's steps become loads (b3d_median, b3d_load_at),
 * with the frame slowed by something else that the Pi's report of
 * 2026-10-05 showed (quad_flat: 25 quads fit, 33 not, 44 fit again) */
static int selftest(void)
{
    int bad = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s\n", #c); bad++; } } while (0)
    float f1[6] = { 12, 12.5f, 95, 12.2f, 12.1f, 12.4f };    /* one frame 95 ms */
    EXPECT(b3d_median(f1, 6) > 12.1f && b3d_median(f1, 6) < 12.5f);
    float f2[1] = { 30 };
    EXPECT(b3d_median(f2, 1) == 30);
    const float n[5] = { 19, 25, 33, 44, 58 };
    const float spike[5] = { 9, 11, 24, 13.1f, 21 };
    const float l60 = b3d_load_at(n, spike, 5, 16.667f);
    EXPECT(l60 > 44 && l60 < 58);                           /* not 28.7 */
    const float even[5] = { 9, 11, 14, 17, 21 };
    const float e60 = b3d_load_at(n, even, 5, 16.667f);
    EXPECT(e60 > 33 && e60 < 44);
    const float slow[3] = { 20, 30, 45 };
    EXPECT(b3d_load_at(n, slow, 3, 16.667f) == -1);
    EXPECT(b3d_load_at(n, slow, 3, 33.333f) > 25 && b3d_load_at(n, slow, 3, 33.333f) < 33);
    const float fast[3] = { 2, 3, 4 };
    EXPECT(b3d_load_at(n, fast, 3, 16.667f) == 33);         /* never over: the last */
    const float first[3] = { 40, 5, 6 };                    /* the first step slowed */
    EXPECT(b3d_load_at(n, first, 3, 16.667f) == 33);
#undef EXPECT
    printf("b3d selftest: %s\n", bad ? "FAILED" : "ok (median of a step, the last step that fits)");
    return bad != 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--selftest"))
        return selftest();
    if (argc < 2) {
        fprintf(stderr, "usage: b3d_host DIR [--full] [-v] [--tests=a,b] [--profiles=P,Q] [--frames]\n");
        return 2;
    }
    dir = argv[1];
    int full = 0;
    const char *only_tests = NULL, *only_profiles = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--full")) full = 1;
        if (!strcmp(argv[i], "-v")) verbose = 1;
        if (!strcmp(argv[i], "--frames")) frames = 1;
        if (!strncmp(argv[i], "--tests=", 8)) only_tests = argv[i] + 8;
        if (!strncmp(argv[i], "--profiles=", 11)) only_profiles = argv[i] + 11;
        if (!strncmp(argv[i], "--stop=", 7)) stop_at = atoi(argv[i] + 7);
    }
    uint16_t *px = test_aligned_alloc(64, 640 * 360 * 2);     /* where the emulated V3D can draw */
    g16_target(&page, px, 640, 640, 360, &font_console_8x16);
    if (gpu3d_init() != 0) {
        fprintf(stderr, "gpu3d: %s (%s)\n", gpu3d_status(), emu_error);
        return 1;
    }
    /* B3D_EMU_SKIP: the jobs counted, not run (the full bench's loads in minutes) */
    emu_skip = getenv("B3D_EMU_SKIP") != NULL;
    b3d_platform_t p = {
        .g = &page, .us = us, .present = present, .count = NULL, .counting = 0, .key = key, .log = log_line,
        .save = save, .load_last = load_last, .kernel = "host", .machine = "PC, the V3D emulated",
        .date = "", .quick = !full, .page_shown = shown, .only_tests = only_tests, .only_profiles = only_profiles,
        .stop = stop_at > 0 ? stop : NULL,
    };
    if (stop_at > 0) {
        /* stopped halfway: B3D_STOPPED, no report saved, no page shown */
        const int before = last_number(), rc = b3d_run(&p);
        FILE *f;
        char path[512];
        snprintf(path, sizeof path, "%s/page-01.ppm", dir);
        const int paged = (f = fopen(path, "rb")) != NULL;
        if (f)
            fclose(f);
        if (rc != B3D_STOPPED || last_number() != before || paged || stop_calls != stop_at) {
            fprintf(stderr, "b3d --stop=%d: rc %d, reports %d -> %d, pages %d, asked %d times\n", stop_at, rc,
                    before, last_number(), paged, stop_calls);
            return 1;
        }
        printf("b3d: stopped at frame %d, no report, no pages\n", stop_at);
        return 0;
    }
    if (b3d_run(&p) != 0) {
        fprintf(stderr, "b3d: the report was not saved\n");
        return 1;
    }
    printf("b3d: report %s/3D%04d.TXT, pages in %s\n", dir, last_number(), dir);
    return 0;
}
