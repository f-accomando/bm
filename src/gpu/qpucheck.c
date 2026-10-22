#include "qpucheck.h"
#include "qpu.h"
#include "lib/printf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PBLOCKS 64                      /* 1024 particles */
#define FPIX    1024                    /* a multiple of 32 */
#define LEVELS  8

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static int close_enough(float a, float b)
{
    return fabsf(a - b) <= 1e-5f * (fabsf(b) > 1.0f ? fabsf(b) : 1.0f);
}

static void note(qpucheck_t *r, const char *msg)
{
    if (!r->why[0])
        ksnprintf(r->why, sizeof r->why, "%s", msg);
}

/* returns QPUCHECK_*, and the times */
static int check_particles(qpucheck_t *r, uint32_t (*us)(void))
{
    enum { N = PBLOCKS * QPU_PBLOCK };
    float *src = qpu_alloc(N * sizeof(float)), *dst = qpu_alloc(N * sizeof(float)), *ref = malloc(N * sizeof(float));
    const float dt = 0.016f, g = 10.0f;
    int res = QPUCHECK_SKIP;
    if (!src || !dst || !ref) {
        note(r, "particles: no memory");
        goto done;
    }
    uint32_t s = 4242;
    for (int i = 0; i < N; i++)
        src[i] = (float)((int)(lcg(&s) % 2001) - 1000) / 10.0f;
    for (int b = 0; b < PBLOCKS; b++)
        for (int l = 0; l < 16; l++)
            src[b * QPU_PBLOCK + 64 + l] = (float)(lcg(&s) % 150) / 100.0f;     /* life 0 .. 1.5 */
    memset(dst, 0, N * sizeof(float));
    r->p_count = PBLOCKS * 16;
    uint32_t t0 = us();
    qpu_particles_arm(src, ref, PBLOCKS, dt, g * dt);
    r->p_arm_us = us() - t0;
    const uint32_t q0 = qpu_stats.on_qpu;
    t0 = us();
    qpu_particles(src, dst, PBLOCKS, dt, g);
    r->p_qpu_us = us() - t0;
    if (qpu_stats.on_qpu == q0) {                           /* the ARM did it: the QPUs failed or timed out */
        note(r, "particles: the QPU run failed");
        goto done;
    }
    res = QPUCHECK_PASS;
    for (int i = 0; i < N; i++)
        if (!close_enough(dst[i], ref[i])) {
            char m[48];
            ksnprintf(m, sizeof m, "particles: word %d differs from the ARM", i);
            note(r, m);
            res = QPUCHECK_FAIL;
            break;
        }
done:
    qpu_free(src);
    qpu_free(dst);
    free(ref);
    return res;
}

static int check_fade(qpucheck_t *r, uint32_t (*us)(void))
{
    uint16_t *src = qpu_alloc(FPIX * 2), *dst = qpu_alloc(FPIX * 2), *ref = malloc(FPIX * 2);
    uint8_t *lv = qpu_alloc(FPIX), *index = qpu_alloc(65536);
    uint16_t *tab = qpu_alloc(LEVELS * 256 * 2);
    static const uint16_t mul[LEVELS][3] = { { 0, 0, 0 }, { 40, 40, 40 }, { 100, 120, 90 }, { 180, 160, 200 },
                                            { 256, 256, 256 }, { 300, 280, 260 }, { 400, 512, 450 }, { 512, 512, 512 } };
    int res = QPUCHECK_SKIP;
    if (!src || !dst || !ref || !lv || !index || !tab) {
        note(r, "fade: no memory");
        goto done;
    }
    uint32_t s = 999;
    memset(index, 255, 65536);
    for (int i = 0; i < LEVELS; i++)
        index[0x2345 + 0x111 * i] = (uint8_t)i;
    for (int i = 0; i < LEVELS * 256; i++)
        tab[i] = (uint16_t)lcg(&s);
    for (int p = 0; p < FPIX; p++) {
        lv[p] = (uint8_t)(lcg(&s) % LEVELS);
        src[p] = p % 3 == 0 ? (uint16_t)(0x2345 + 0x111 * (lcg(&s) % LEVELS)) : (uint16_t)lcg(&s);
    }
    memset(dst, 0, FPIX * 2);
    qpu_fade_t f = { .src = src, .dst = dst, .pixels = FPIX, .lv = lv, .index = index, .tab = tab, .mul = mul,
                     .levels = LEVELS };
    r->f_count = FPIX;
    qpu_fade_t a = f;
    a.dst = ref;
    uint32_t t0 = us();
    qpu_fade_arm(&a);
    r->f_arm_us = us() - t0;
    const uint32_t q0 = qpu_stats.on_qpu;
    t0 = us();
    qpu_fade(&f);
    r->f_qpu_us = us() - t0;
    if (qpu_stats.on_qpu == q0) {
        note(r, "fade: the QPU run failed");
        goto done;
    }
    res = QPUCHECK_PASS;
    for (int i = 0; i < FPIX; i++)
        if (dst[i] != ref[i]) {
            char m[48];
            ksnprintf(m, sizeof m, "fade: pixel %d is %04x, wanted %04x", i, dst[i], ref[i]);
            note(r, m);
            res = QPUCHECK_FAIL;
            break;
        }
done:
    qpu_free(src);
    qpu_free(dst);
    free(ref);
    qpu_free(lv);
    qpu_free(index);
    qpu_free(tab);
    return res;
}

int qpu_check(qpucheck_t *r, uint32_t (*us)(void))
{
    memset(r, 0, sizeof *r);
    r->particles = r->fade = QPUCHECK_SKIP;
    if (qpu_init() != 0) {
        ksnprintf(r->why, sizeof r->why, "qpu_init: %s", qpu_status());
        return QPUCHECK_SKIP;
    }
    if (qpu_particles_ok())
        r->particles = check_particles(r, us);
    else
        note(r, "particles off");
    if (qpu_fade_ok())
        r->fade = check_fade(r, us);
    else
        note(r, "fade off");
    if (r->particles == QPUCHECK_FAIL || r->fade == QPUCHECK_FAIL)
        return QPUCHECK_FAIL;
    if (r->particles == QPUCHECK_PASS || r->fade == QPUCHECK_PASS)
        return QPUCHECK_PASS;
    return QPUCHECK_SKIP;
}

const char *qpu_check_word(int result)
{
    return result == QPUCHECK_PASS ? "PASS" : result == QPUCHECK_FAIL ? "FAIL" : "SKIP";
}

void qpu_check_line(const qpucheck_t *r, int overall, char *out, unsigned size)
{
    ksnprintf(out, size, "QPU %s: particles %s (%lu: ARM %lu us, QPU %lu us), fade %s (%lu px: ARM %lu us, QPU %lu us)%s%s",
              qpu_check_word(overall), qpu_check_word(r->particles), (unsigned long)r->p_count,
              (unsigned long)r->p_arm_us, (unsigned long)r->p_qpu_us, qpu_check_word(r->fade),
              (unsigned long)r->f_count, (unsigned long)r->f_arm_us, (unsigned long)r->f_qpu_us,
              r->why[0] ? "; " : "", r->why);
}
