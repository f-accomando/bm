#include "roombench.h"
#include "runtime.h"
#include "gpu/gpu3d.h"
#include "gpu/v3d.h"
#include "kernel/syskeys.h"
#include "lib/printf.h"

#include <string.h>

extern const uint8_t bm_texroom_cart[], bm_texroom_cart_end[];

#define STEP_S      2
#define FIRST       8
#define MAX_CRATES  4096
#define FPS60       590             /* tenths: a dropped frame in 2 s is still 60 */
#define FPS30       295

typedef struct {
    int crates60, crates30;
    uint32_t tris60, tris30;
    int ran;
    const char *why;                /* why it stopped early, or NULL */
} result_t;

static void line(const char *label, int crates, const bm_stats_t *st, uint32_t fps10)
{
    const uint32_t us = st->frames ? st->cpu_us_total / st->frames : 0;
    kprintf("%-13s %6d %10lu %7lu.%lu ms %5lu.%lu fps\n", label, crates, st->tris3d, us / 1000,
            us / 100 % 10, fps10 / 10, fps10 % 10);
}

/* one case: 0 if it went until under 30 fps (or the most crates), -1 if
 * the user stopped it */
static int run_case(framebuffer_t *fb, int w, int h, int gpu, result_t *r)
{
    char label[16];
    ksnprintf(label, sizeof label, "%s %dx%d", gpu ? "GPU" : "ARM", w, h);
    memset(r, 0, sizeof *r);
    r->ran = 1;
    const size_t len = (size_t)(bm_texroom_cart_end - bm_texroom_cart);
    for (int n = FIRST; n <= MAX_CRATES; n *= 2) {
        bm_stats_t st;
        bm_next_run(w, h, gpu, n);
        bm_play(fb, bm_texroom_cart, len, STEP_S, &st);
        if (!st.ok || !st.frames) {
            r->why = "the cartridge stopped with an error";
            kprintf("%-13s %6d  %s\n", label, n, r->why);
            return 0;
        }
        if (st.left || st.elapsed_us + 300000u < STEP_S * 1000000u) {
            kprintf("%-13s %6d  stopped\n", label, n);
            syskeys_test_set_stopped();     /* PS, Ctrl+Esc, Start+Select: no report (syskeys.h) */
            return -1;
        }
        if (gpu && !st.gpu3d) {
            r->why = gpu3d_status();
            kprintf("%-13s %6d  the GPU did not draw: %s\n", label, n, r->why);
            return 0;
        }
        const uint32_t fps10 = (uint32_t)((uint64_t)st.frames * 10000000u / st.elapsed_us);
        line(label, n, &st, fps10);
        if (fps10 >= FPS60) {
            r->crates60 = n;
            r->tris60 = st.tris3d;
        }
        if (fps10 < FPS30)
            return 0;
        r->crates30 = n;
        r->tris30 = st.tris3d;
    }
    return 0;
}

void bm_room_bench(framebuffer_t *fb)
{
    static const struct { int w, h; } res[2] = { { 320, 180 }, { 640, 360 } };
    result_t sum[2][2];
    memset(sum, 0, sizeof sum);
    const int gpu = v3d_init() == 0;
    syskeys_test_begin();
    kprintf("Texture Room: crates doubled from %d while it keeps 30 fps, %d s each\n", FIRST, STEP_S);
    kprintf("(ms: update + draw of a frame, the GPU's work included)\n");
    kprintf("%-13s %6s %10s\n", "", "crates", "triangles");
    int stopped = 0;
    for (int g = 0; g <= gpu && !stopped; g++)
        for (int k = 0; k < 2 && !stopped; k++)
            stopped = run_case(fb, res[k].w, res[k].h, g, &sum[g][k]) != 0;
    if (!gpu)
        kprintf("GPU: none (%s)\n", v3d_status());

    kprintf("\x1b[1mmost crates (triangles) %10s %18s\x1b[0m\n", "at 60 fps", "at 30 fps");
    for (int g = 0; g <= gpu; g++)
        for (int k = 0; k < 2; k++) {
            const result_t *r = &sum[g][k];
            if (!r->ran)
                continue;
            char a[24], b[24];
            if (r->crates60) ksnprintf(a, sizeof a, "%d (%lu)", r->crates60, r->tris60);
            else ksnprintf(a, sizeof a, "<%d", FIRST);
            if (r->crates30) ksnprintf(b, sizeof b, "%d (%lu)", r->crates30, r->tris30);
            else ksnprintf(b, sizeof b, "<%d", FIRST);
            kprintf("%s %dx%d %22s %18s%s\n", g ? "GPU" : "ARM", res[k].w, res[k].h, a, b,
                    r->why ? " (stopped early)" : "");
        }
    kprintf(stopped ? "Texture Room benchmark stopped\n" : "Texture Room benchmark done\n");
}
