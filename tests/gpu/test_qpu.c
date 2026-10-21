/*
 * QPU programs (src/gpu/qpu.c, tools/qpuprogs.py) on the V3D emulator
 * (tests/gpu/v3d_emu.c): every program gives the ARM version's result
 * (particles to the bit, the fade exactly), a queue of requests runs in
 * batches, and a V3D that does not answer, or answers wrong, leaves the ARM
 * versions in charge with the same results.
 *
 *   test_qpu 0     the QPUs work (then the V3D stops answering)
 *   test_qpu 1     the QPUs never answer: probe off, ARM results
 *   test_qpu 2     the QPUs store wrong data: probe off, ARM results
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/qpu.h"
#include "v3d_emu.h"

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static uint32_t seed = 99;
static uint32_t rnd(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

static float frnd(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd() % 100001) / 100000.0f;
}

static void fill_particles(float *a, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++)
        for (int l = 0; l < 16; l++) {
            float *p = a + (size_t)b * QPU_PBLOCK;
            p[l] = frnd(-300, 300);
            p[16 + l] = frnd(-300, 300);
            p[32 + l] = frnd(-50, 50);
            p[48 + l] = frnd(-50, 50);
            p[64 + l] = frnd(0, 1.5f);
        }
}

/* particles: a request of `blocks` blocks must equal the ARM version to the bit */
static void particles_case(uint32_t blocks, int expect_qpu)
{
    const size_t n = (size_t)blocks * QPU_PBLOCK;
    float *src = qpu_alloc(n * 4), *dst = qpu_alloc(n * 4), *ref = malloc(n * 4);
    fill_particles(src, blocks);
    float *keep = malloc(n * 4);
    memcpy(keep, src, n * 4);
    memset(dst, 0xAB, n * 4);
    const float dt = 1.0f / 60.0f, g = 9.8f;
    const uint32_t jobs0 = emu_stats.qpu_jobs;
    qpu_particles(src, dst, blocks, dt, g);
    const float gdt = g * dt;                                   /* the request keeps g * dt */
    qpu_particles_arm(keep, ref, blocks, dt, gdt);
    CHECK(!memcmp(dst, ref, n * 4), "particles, %u blocks: not the ARM result", blocks);
    CHECK(!memcmp(src, keep, n * 4), "particles, %u blocks: the source changed", blocks);
    CHECK((emu_stats.qpu_jobs > jobs0) == expect_qpu, "particles, %u blocks: %u QPU jobs, expected %s", blocks,
          emu_stats.qpu_jobs - jobs0, expect_qpu ? "some" : "none");
    qpu_free(src);
    qpu_free(dst);
    free(ref);
    free(keep);
}

typedef struct {
    uint16_t *src, *dst;
    uint8_t *lv, *index;
    uint16_t *tab;
    uint16_t (*mul)[3];
    int levels;
    uint32_t pixels;
} fade_data_t;

static void fade_make(fade_data_t *d, uint32_t pixels, int levels, int ntab)
{
    d->pixels = pixels;
    d->levels = levels;
    d->src = qpu_alloc(pixels * 2);
    d->dst = qpu_alloc(pixels * 2);
    d->lv = qpu_alloc(pixels);
    d->index = qpu_alloc(65536);
    d->tab = qpu_alloc(16 * 256 * 2);
    d->mul = malloc(16 * sizeof *d->mul);
    memset(d->index, 255, 65536);
    uint16_t from[256];
    for (int i = 0; i < ntab; i++) {
        from[i] = (uint16_t)rnd();
        d->index[from[i]] = (uint8_t)i;
    }
    for (int i = 0; i < 16 * 256; i++)
        d->tab[i] = (uint16_t)rnd();
    for (int k = 0; k < 16; k++)
        for (int c = 0; c < 3; c++)
            d->mul[k][c] = (uint16_t)(k == 0 ? 0 : k == 1 ? 512 : rnd() % 513);
    for (uint32_t p = 0; p < pixels; p++) {
        d->lv[p] = (uint8_t)(rnd() % (uint32_t)levels);
        d->src[p] = ntab && rnd() % 3 == 0 ? from[rnd() % (uint32_t)ntab] : (uint16_t)rnd();
    }
    memset(d->dst, 0x5A, pixels * 2);
}

static void fade_free(fade_data_t *d)
{
    qpu_free(d->src);
    qpu_free(d->dst);
    qpu_free(d->lv);
    qpu_free(d->index);
    qpu_free(d->tab);
    free(d->mul);
}

static qpu_fade_t fade_req(const fade_data_t *d)
{
    qpu_fade_t f = { .src = d->src, .dst = d->dst, .pixels = d->pixels, .lv = d->lv, .index = d->index,
                     .tab = d->tab, .mul = (const uint16_t (*)[3])d->mul, .levels = d->levels };
    return f;
}

static void fade_case(uint32_t pixels, int levels, int ntab, int expect_qpu)
{
    fade_data_t d;
    fade_make(&d, pixels, levels, ntab);
    qpu_fade_t f = fade_req(&d);
    uint16_t *ref = malloc(pixels * 2);
    qpu_fade_t r = f;
    r.dst = ref;
    qpu_fade_arm(&r);
    const uint32_t jobs0 = emu_stats.qpu_jobs;
    qpu_fade(&f);
    CHECK(!memcmp(d.dst, ref, pixels * 2), "fade, %u pixels, %d levels, %d tables: not the ARM result", pixels, levels,
          ntab);
    CHECK((emu_stats.qpu_jobs > jobs0) == expect_qpu, "fade, %u pixels: %u QPU jobs", pixels, emu_stats.qpu_jobs - jobs0);
    free(ref);
    fade_free(&d);
}

/* g16_fade_apply of the 2D library against qpu_fade_apply, on lamps and colours made with its own calls */
static void g16_case(int w, int h, int expect_qpu, int misalign)
{
    g16_fade_t f;
    g16_fade_init(&f, w, h);
    /* the tables and the level map in memory the V3D reads */
    uint8_t *lv = qpu_alloc((size_t)w * h), *index = qpu_alloc(65536);
    uint16_t *tab = qpu_alloc(G16_FADE_LEVELS * 256 * 2);
    g16_fade_reset(&f, 8);
    for (int i = 0; i < 12; i++) {
        uint16_t to[G16_FADE_LEVELS];
        const uint16_t from = (uint16_t)rnd();
        for (int k = 0; k < f.levels; k++)
            to[k] = (uint16_t)((from >> 1 & 0x7BEF) * (uint32_t)(k + 1) / (uint32_t)f.levels);
        g16_fade_colour(&f, from, to);
    }
    g16_fade_done(&f);
    g16_fade_clear(&f, 1);
    for (int i = 0; i < 5; i++)
        g16_fade_glow(&f, (int)(rnd() % (uint32_t)w), (int)(rnd() % (uint32_t)h), 20 + (int)(rnd() % 80), 7,
                      (int)(rnd() % 257));
    memcpy(lv, f.lv, (size_t)w * h);
    memcpy(index, f.index, 65536);
    memcpy(tab, f.tab, G16_FADE_LEVELS * 256 * 2);
    free(f.lv);
    free(f.index);
    free(f.tab);
    f.lv = lv;
    f.index = index;
    f.tab = tab;
    uint16_t *px0 = qpu_alloc((size_t)w * h * 2 + 64), *px = px0 + (misalign ? 1 : 0), *ref = malloc((size_t)w * h * 2);
    for (int i = 0; i < w * h; i++) {
        px[i] = (uint16_t)rnd();
        if (i % 4 == 0)
            for (int c = 0; c < 65536 && f.ncol; c += 7919)    /* some colours that have a table */
                if (f.index[c] != 255 && rnd() % 8 == 0) {
                    px[i] = (uint16_t)c;
                    break;
                }
    }
    memcpy(ref, px, (size_t)w * h * 2);
    g16_t g = { .px = px, .stride = (uint32_t)w, .w = w, .h = h };
    g16_t gr = g;
    gr.px = ref;
    g16_fade_apply(&gr, &f);
    const uint32_t jobs0 = emu_stats.qpu_jobs;
    const int by_qpu = qpu_fade_apply(&g, &f);
    CHECK(!memcmp(px, ref, (size_t)w * h * 2), "g16 fade %dx%d: not the library's picture", w, h);
    CHECK(by_qpu == expect_qpu && (emu_stats.qpu_jobs > jobs0) == expect_qpu, "g16 fade %dx%d: by_qpu %d, jobs %u", w, h,
          by_qpu, emu_stats.qpu_jobs - jobs0);
    qpu_free(px0);
    free(ref);
    qpu_free(lv);
    qpu_free(index);
    qpu_free(tab);
}

int main(int argc, char **argv)
{
    const int mode = argc > 1 ? atoi(argv[1]) : 0;
    emu_qpu = mode;
    const int r = qpu_init();
    printf("qpu: %s\n", qpu_status());
    if (emu_error[0])
        printf("emulator: %s\n", emu_error);
    if (mode == 0) {
        CHECK(r == 0 && qpu_particles_ok() && qpu_fade_ok(), "probe failed: %s", qpu_status());
        CHECK(qpu_stats.probe_bad == 0, "probe turned %u programs off", qpu_stats.probe_bad);
        for (uint32_t b = 1; b <= 40; b += (b < 8 ? 1 : 7))
            particles_case(b, 1);
        fade_case(32, 8, 3, 1);
        fade_case(64, 16, 0, 1);
        fade_case(96, 2, 5, 1);
        fade_case(32 * 13, 8, 200, 1);
        fade_case(32 * 50, 12, 255, 1);
        fade_case(32 * 400, 8, 7, 1);       /* 12 jobs of 33 blocks */
        g16_case(640, 360, 1, 0);              /* a screen of Yharnam's size */
        g16_case(100, 33, 1, 0);               /* 3300 pixels: 103 blocks and 4 pixels for the ARM */
        g16_case(20, 1, 0, 0);               /* less than a block */
        g16_case(64, 32, 0, 1);             /* a picture not on a 64-byte line: the ARM, same picture */
        CHECK(emu_stats.qpu_dma > 0, "no DMA store counted");

        /* a queue: three requests of different kinds, one flush, one launch each batch */
        const size_t n1 = 5 * QPU_PBLOCK, n2 = 9 * QPU_PBLOCK;
        float *s1 = qpu_alloc(n1 * 4), *d1 = qpu_alloc(n1 * 4), *s2 = qpu_alloc(n2 * 4), *d2 = qpu_alloc(n2 * 4);
        fill_particles(s1, 5);
        fill_particles(s2, 9);
        fade_data_t fd;
        fade_make(&fd, 32 * 6, 8, 20);
        qpu_fade_t ff = fade_req(&fd);
        const uint32_t runs0 = qpu_stats.runs, q0 = qpu_stats.on_qpu;
        qpu_particles_queue(s1, d1, 5, 0.02f, -9.0f);
        qpu_fade_queue(&ff);
        qpu_particles_queue(s2, d2, 9, 0.03f, 4.0f);
        CHECK(qpu_stats.runs == runs0, "queued requests ran before the flush");
        CHECK(qpu_flush() == 0, "flush: the ARM did some");
        CHECK(qpu_stats.runs == runs0 + 3 && qpu_stats.on_qpu == q0 + 3, "3 queued: runs %u on_qpu %u",
              qpu_stats.runs - runs0, qpu_stats.on_qpu - q0);
        float *e1 = malloc(n1 * 4), *e2 = malloc(n2 * 4);
        qpu_particles_arm(s1, e1, 5, 0.02f, -9.0f * 0.02f);
        qpu_particles_arm(s2, e2, 9, 0.03f, 4.0f * 0.03f);
        CHECK(!memcmp(d1, e1, n1 * 4) && !memcmp(d2, e2, n2 * 4), "queued particles differ");
        uint16_t *ef = malloc(fd.pixels * 2);
        qpu_fade_t rf = ff;
        rf.dst = ef;
        qpu_fade_arm(&rf);
        CHECK(!memcmp(fd.dst, ef, fd.pixels * 2), "queued fade differs");
        /* more requests than the queue holds: the extra ones flush on their own */
        for (int i = 0; i < 20; i++)
            qpu_particles_queue(s1, d1, 5, 0.02f, -9.0f);
        qpu_flush();
        CHECK(!memcmp(d1, e1, n1 * 4), "a long queue differs");
        free(e1);
        free(e2);
        free(ef);
        fade_free(&fd);
        qpu_free(s1);
        qpu_free(d1);
        qpu_free(s2);
        qpu_free(d2);

        /* a shape the QPU programs do not take (pixels not a multiple of 32): the ARM, same result */
        fade_case(40, 8, 4, 0);

        /* the V3D stops answering after a good probe: the ARM results, then the QPU path is off */
        emu_qpu = 1;
        const uint32_t f0 = qpu_stats.failures, a0 = qpu_stats.on_arm;
        particles_case(7, 0);               /* the jobs time out, the result is the ARM's */
        CHECK(qpu_stats.failures == f0 + 1 && qpu_stats.on_arm == a0 + 1, "one failed run: failures %u arm %u",
              qpu_stats.failures - f0, qpu_stats.on_arm - a0);
        CHECK(qpu_particles_ok(), "off after one failure");
        particles_case(7, 0);
        CHECK(!qpu_particles_ok() && !qpu_fade_ok(), "still on after two failures");
        emu_qpu = 0;
        particles_case(7, 0);               /* off for good: no job even when the V3D answers again */
        fade_case(64, 8, 4, 0);
        g16_case(64, 32, 0, 0);
    } else {
        CHECK(r == -1 && !qpu_particles_ok() && !qpu_fade_ok(), "QPUs usable with emu_qpu=%d: %s", mode, qpu_status());
        if (mode == 1) {
            CHECK(qpu_stats.probe_bad == 1 && strstr(qpu_status(), "skipped"), "the fade probe waited too: %s", qpu_status());
            CHECK(emu_stats.qpu_jobs == 0, "jobs ran with a V3D that never answers");
        }
        if (mode == 2)
            CHECK(qpu_stats.probe_bad == 2, "probe turned %u programs off", qpu_stats.probe_bad);
        emu_qpu = 0;
        particles_case(9, 0);
        fade_case(96, 8, 4, 0);
        g16_case(64, 32, 0, 0);
    }
    CHECK(test_arena_overruns() == 0, "a block was written past its end");
    printf("qpu: %d/%d checks passed (%u QPU jobs, %u instructions)\n", checks - failures, checks, emu_stats.qpu_jobs,
           emu_stats.qpu_steps);
    return failures ? 1 : 0;
}
