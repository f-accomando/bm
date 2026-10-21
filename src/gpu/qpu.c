#include "qpu.h"
#include "qpuprog.h"
#include "v3d.h"
#include "lib/printf.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

qpu_stats_t qpu_stats;

#define QREQ        8                   /* requests queued before a flush */
#define QJOBS       12                  /* QPUs a request is split over */

enum { K_PARTICLES, K_FADE };

typedef struct {
    int kind;
    uint32_t blocks;                    /* particles: blocks of 16; fade: blocks of 32 pixels */
    /* particles */
    const float *psrc;
    float *pdst;
    float dt, gdt;
    /* fade */
    qpu_fade_t f;
    uint32_t *mulp;                     /* its scales, packed (in memory the V3D reads) */
    int arm_only;                       /* a shape the QPU programs do not take */
} req_t;

static int inited, off;
static int particles_ok, fade_ok;
static int fail_run;                    /* failed runs in a row */
static char status[192] = "not started";
static uint32_t *code_mem;              /* the programs, in memory the V3D reads */
static uint32_t code_off[2];            /* their bus addresses */
static uint32_t (*uni)[8];              /* the uniforms of the jobs of a batch */
static uint32_t *mulp_pool;             /* QREQ x 16 words */
static req_t reqs[QREQ];
static int nreq;

void *qpu_alloc(size_t bytes)
{
    return aligned_alloc(64, (bytes + 63) & ~(size_t)63);
}

void qpu_free(void *p)
{
    free(p);
}

const char *qpu_status(void)
{
    return status;
}

int qpu_particles_ok(void)
{
    return particles_ok && !off;
}

int qpu_fade_ok(void)
{
    return fade_ok && !off;
}

void qpu_disable(void)
{
    off = 1;
}

/* ------------------------------------------------------------ ARM versions */

void qpu_particles_arm(const float *src, float *dst, uint32_t blocks, float dt, float gdt)
{
    for (uint32_t b = 0; b < blocks; b++) {
        const float *s = src + (size_t)b * QPU_PBLOCK;
        float *d = dst + (size_t)b * QPU_PBLOCK;
        for (int l = 0; l < 16; l++) {
            const float x = s[l], y = s[16 + l], vx = s[32 + l], vy = s[48 + l], life = s[64 + l];
            const float mx = vx * dt, my = vy * dt;      /* separate, as the QPU's two ALU ops */
            d[l] = x + mx;
            d[16 + l] = y + my;
            d[32 + l] = vx;
            d[48 + l] = vy + gdt;
            d[64 + l] = fmaxf(life - dt, 0.0f);
        }
    }
}

void qpu_fade_arm(const qpu_fade_t *f)
{
    for (uint32_t p = 0; p < f->pixels; p++) {
        const uint16_t c = f->src[p];
        const int k = f->lv[p], i = f->index[c];
        if (i != 255) {
            f->dst[p] = f->tab[k * 256 + i];
        } else {
            const uint16_t *m = f->mul[k];
            uint32_t r5 = ((uint32_t)(c >> 11) * m[0]) >> 8;
            uint32_t g6 = ((uint32_t)(c >> 5 & 63) * m[1]) >> 8;
            uint32_t b5 = ((uint32_t)(c & 31) * m[2]) >> 8;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            f->dst[p] = (uint16_t)(r5 << 11 | g6 << 5 | b5);
        }
    }
}

static void run_arm(const req_t *r)
{
    if (r->kind == K_PARTICLES) {
        qpu_particles_arm(r->psrc, r->pdst, r->blocks, r->dt, r->gdt);
    } else {
        qpu_fade_arm(&r->f);
    }
    qpu_stats.on_arm++;
}

/* ------------------------------------------------------------------ QPU path */

static uint32_t fbits(float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    return u;
}

/* the jobs of request r (up to QJOBS) into jobs[], uniforms from uni[at]; the count */
static int make_jobs(const req_t *r, v3d_qpu_job_t *jobs, int at)
{
    const uint32_t nj = r->blocks < QJOBS ? r->blocks : QJOBS;
    for (uint32_t j = 0; j < nj; j++) {
        const uint32_t b0 = r->blocks * j / nj, b1 = r->blocks * (j + 1) / nj;
        uint32_t *u = uni[at + (int)j];
        if (r->kind == K_PARTICLES) {
            u[0] = v3d_bus(r->psrc + (size_t)b0 * QPU_PBLOCK);
            u[1] = v3d_bus(r->pdst + (size_t)b0 * QPU_PBLOCK);
            u[2] = b1 - b0;
            u[3] = fbits(r->dt);
            u[4] = fbits(r->gdt);
            jobs[j].code = code_off[K_PARTICLES];
            jobs[j].nunif = 5;
        } else {
            u[0] = v3d_bus(r->f.src + (size_t)b0 * 32);
            u[1] = v3d_bus(r->f.dst + (size_t)b0 * 32);
            u[2] = v3d_bus(r->f.lv + (size_t)b0 * 32);
            u[3] = v3d_bus(r->f.index);
            u[4] = v3d_bus(r->f.tab);
            u[5] = v3d_bus(r->mulp);
            u[6] = b1 - b0;
            jobs[j].code = code_off[K_FADE];
            jobs[j].nunif = 7;
        }
        jobs[j].unif = v3d_bus(u);
    }
    return (int)nj;
}

/* requests [first, first + n) as one launch (<= V3D_QPU_MAX jobs): 0, or -1 */
static int run_batch(const req_t *r, int n)
{
    v3d_qpu_job_t jobs[V3D_QPU_MAX];
    int nj = 0;
    for (int i = 0; i < n; i++)
        nj += make_jobs(&r[i], jobs + nj, nj);
    qpu_stats.jobs += (uint32_t)nj;
    return v3d_qpu_run(jobs, nj, QPU_TIMEOUT_US);
}

static int usable(const req_t *r)
{
    return !off && inited && !r->arm_only && (r->kind == K_PARTICLES ? particles_ok : fade_ok);
}

static int jobs_of(const req_t *r)
{
    return (int)(r->blocks < QJOBS ? r->blocks : QJOBS);
}

static int flush_reqs(req_t *r, int n)
{
    int arm = 0;
    for (int i = 0; i < n;) {
        if (!usable(&r[i])) {
            run_arm(&r[i]);
            qpu_stats.runs++;
            arm++;
            i++;
            continue;
        }
        int m = 0, nj = 0;
        while (i + m < n && usable(&r[i + m]) && nj + jobs_of(&r[i + m]) <= V3D_QPU_MAX) {
            nj += jobs_of(&r[i + m]);
            m++;
        }
        if (run_batch(r + i, m) == 0) {
            fail_run = 0;
            qpu_stats.on_qpu += (uint32_t)m;
            qpu_stats.runs += (uint32_t)m;
        } else {
            qpu_stats.failures++;
            if (++fail_run >= 2)
                off = 1;                /* the QPUs do not answer: the ARM from now on */
            for (int k = 0; k < m; k++) {
                run_arm(&r[i + k]);     /* the sources are untouched: from the start */
                qpu_stats.runs++;
            }
            arm += m;
        }
        i += m;
    }
    return arm;
}

int qpu_flush(void)
{
    const int n = nreq;
    nreq = 0;
    return flush_reqs(reqs, n);
}

static req_t *slot(void)
{
    if (nreq >= QREQ)
        qpu_flush();
    return &reqs[nreq++];
}

int qpu_particles_queue(const float *src, float *dst, uint32_t blocks, float dt, float g)
{
    if (!blocks)
        return 0;
    req_t *r = slot();
    memset(r, 0, sizeof *r);
    r->kind = K_PARTICLES;
    r->blocks = blocks;
    r->psrc = src;
    r->pdst = dst;
    r->dt = dt;
    r->gdt = g * dt;
    return 0;
}

int qpu_particles(const float *src, float *dst, uint32_t blocks, float dt, float g)
{
    qpu_particles_queue(src, dst, blocks, dt, g);
    return qpu_flush();
}

int qpu_fade_queue(const qpu_fade_t *f)
{
    if (!f->pixels)
        return 0;
    req_t *r = slot();
    memset(r, 0, sizeof *r);
    r->kind = K_FADE;
    r->f = *f;
    r->blocks = f->pixels / 32;
    if (inited && mulp_pool) {
        r->mulp = mulp_pool + 16 * (r - reqs);
        for (int k = 0; k < 16; k++) {
            const int kk = k < f->levels ? k : f->levels - 1;
            r->mulp[k] = (uint32_t)f->mul[kk][0] | (uint32_t)f->mul[kk][1] << 10 | (uint32_t)f->mul[kk][2] << 20;
        }
    }
    if (f->pixels % 32 || f->levels < 1 || f->levels > 16)
        r->arm_only = 1;
    return 0;
}

int qpu_fade(const qpu_fade_t *f)
{
    qpu_fade_queue(f);
    return qpu_flush();
}

int qpu_fade_apply(g16_t *g, const g16_fade_t *f)
{
    static uint16_t *scratch;
    static uint32_t scratch_px;
    const uint32_t total = (uint32_t)g->w * (uint32_t)g->h, blocks = total / 32;
    if (!qpu_fade_ok() || (((uintptr_t)g->px | (uintptr_t)f->lv | (uintptr_t)f->index | (uintptr_t)f->tab) & 63) ||
        g->stride != (uint32_t)g->w || g->w != f->w || g->h != f->h || blocks == 0 ||
        f->levels > 16) {
        g16_fade_apply(g, f);
        return 0;
    }
    if (scratch_px < blocks * 32) {
        qpu_free(scratch);
        scratch = qpu_alloc((size_t)blocks * 64);
        scratch_px = scratch ? blocks * 32 : 0;
        if (!scratch) {
            g16_fade_apply(g, f);
            return 0;
        }
    }
    qpu_fade_t r = { .src = g->px, .dst = scratch, .pixels = blocks * 32, .lv = f->lv, .index = f->index,
                     .tab = f->tab, .mul = (const uint16_t (*)[3])f->mul, .levels = f->levels };
    const uint32_t q0 = qpu_stats.on_qpu;
    qpu_fade(&r);
    if (qpu_stats.on_qpu == q0) {       /* the ARM did it, into the scratch: from the start, in place */
        g16_fade_apply(g, f);
        return 0;
    }
    memcpy(g->px, scratch, (size_t)blocks * 64);
    if (blocks * 32 < total) {          /* the last pixels, less than a block */
        qpu_fade_t t = r;
        t.src = g->px + blocks * 32;
        t.dst = g->px + blocks * 32;
        t.lv = f->lv + blocks * 32;
        t.pixels = total - blocks * 32;
        qpu_fade_arm(&t);
    }
    return 1;
}

/* ---------------------------------------------------------------------- probe */

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static int close_enough(float a, float b)
{
    return fabsf(a - b) <= 1e-5f * (fabsf(b) > 1.0f ? fabsf(b) : 1.0f);
}

static char why_p[64], why_f[64];

static int probe_particles(void)
{
    enum { BLOCKS = 3, N = BLOCKS * QPU_PBLOCK };
    float *src = qpu_alloc(N * sizeof(float)), *dst = qpu_alloc(N * sizeof(float)), *ref = malloc(N * sizeof(float));
    int ok = 0;
    if (src && dst && ref) {
        uint32_t s = 12345;
        for (int i = 0; i < N; i++)
            src[i] = (float)((int)(lcg(&s) % 2001) - 1000) / 100.0f;
        for (int b = 0; b < BLOCKS; b++)                 /* lives: 0 .. 1 s */
            for (int l = 0; l < 16; l++)
                src[b * QPU_PBLOCK + 64 + l] = (float)(lcg(&s) % 100) / 100.0f;
        memset(dst, 0, N * sizeof(float));
        particles_ok = 1;               /* for the probe's own request */
        req_t r = { .kind = K_PARTICLES, .blocks = BLOCKS, .psrc = src, .pdst = dst, .dt = 0.25f, .gdt = 0.125f };
        const int run = run_batch(&r, 1);
        qpu_particles_arm(src, ref, BLOCKS, 0.25f, 0.125f);
        ok = run == 0;
        if (!ok)
            ksnprintf(why_p, sizeof why_p, "run: %s", v3d_qpu_status());
        for (int i = 0; ok && i < N; i++) {
            ok = close_enough(dst[i], ref[i]);
            if (!ok)
                ksnprintf(why_p, sizeof why_p, "word %d is %08lx, wanted %08lx", i, (unsigned long)fbits(dst[i]),
                          (unsigned long)fbits(ref[i]));
        }
    }
    qpu_free(src);
    qpu_free(dst);
    free(ref);
    return ok;
}

static int probe_fade(void)
{
    enum { PIX = 96 };
    uint16_t *src = qpu_alloc(PIX * 2), *dst = qpu_alloc(PIX * 2), ref[PIX];
    uint8_t *lv = qpu_alloc(PIX), *index = qpu_alloc(65536);
    uint16_t *tab = qpu_alloc(8 * 256 * 2);
    uint32_t *mulp = qpu_alloc(16 * 4);
    static const uint16_t mul[8][3] = { { 0, 0, 0 }, { 40, 40, 40 }, { 100, 120, 90 }, { 180, 160, 200 },
                                        { 256, 256, 256 }, { 300, 280, 260 }, { 400, 512, 450 }, { 512, 512, 512 } };
    int ok = 0;
    if (src && dst && lv && index && tab && mulp) {
        uint32_t s = 777;
        memset(index, 255, 65536);
        for (int i = 0; i < 8; i++)
            index[0x1234 + 0x111 * i] = (uint8_t)i;     /* eight colours with a table */
        for (int i = 0; i < 8 * 256; i++)
            tab[i] = (uint16_t)lcg(&s);
        for (int p = 0; p < PIX; p++) {
            lv[p] = (uint8_t)(lcg(&s) % 8);
            src[p] = p % 3 == 0 ? (uint16_t)(0x1234 + 0x111 * (lcg(&s) % 8)) : (uint16_t)lcg(&s);
        }
        for (int k = 0; k < 16; k++) {
            const int kk = k < 8 ? k : 7;
            mulp[k] = (uint32_t)mul[kk][0] | (uint32_t)mul[kk][1] << 10 | (uint32_t)mul[kk][2] << 20;
        }
        qpu_fade_t f = { .src = src, .dst = dst, .pixels = PIX, .lv = lv, .index = index, .tab = tab, .mul = mul,
                         .levels = 8 };
        memset(dst, 0, PIX * 2);
        fade_ok = 1;
        req_t r = { .kind = K_FADE, .blocks = PIX / 32, .f = f, .mulp = mulp };
        const int run = run_batch(&r, 1);
        f.dst = ref;
        qpu_fade_arm(&f);
        ok = run == 0 && !memcmp(dst, ref, sizeof ref);
        if (run)
            ksnprintf(why_f, sizeof why_f, "run: %s", v3d_qpu_status());
        else if (!ok)
            for (int i = 0; i < PIX; i++)
                if (dst[i] != ref[i]) {
                    ksnprintf(why_f, sizeof why_f, "pixel %d is %04x, wanted %04x", i, dst[i], ref[i]);
                    break;
                }
    }
    qpu_free(src);
    qpu_free(dst);
    qpu_free(lv);
    qpu_free(index);
    qpu_free(tab);
    qpu_free(mulp);
    return ok;
}

int qpu_init(void)
{
    if (inited)
        return particles_ok || fade_ok ? 0 : -1;
    inited = 1;
    if (v3d_init() != 0) {
        ksnprintf(status, sizeof status, "no V3D (%s)", v3d_status());
        return -1;
    }
    const size_t words = sizeof qp_particles / 4 + sizeof qp_fade / 4;
    code_mem = qpu_alloc(words * 4 + 64);
    uni = qpu_alloc(sizeof *uni * V3D_QPU_MAX);
    mulp_pool = qpu_alloc(16 * 4 * QREQ);
    if (!code_mem || !uni || !mulp_pool) {
        ksnprintf(status, sizeof status, "out of memory");
        return -1;
    }
    memcpy(code_mem, qp_particles, sizeof qp_particles);
    memcpy(code_mem + sizeof qp_particles / 4, qp_fade, sizeof qp_fade);
    code_off[K_PARTICLES] = v3d_bus(code_mem);
    code_off[K_FADE] = v3d_bus(code_mem + sizeof qp_particles / 4);
    particles_ok = probe_particles();
    if (!particles_ok)
        qpu_stats.probe_bad++;
    if (!particles_ok && !strcmp(v3d_qpu_status(), "timeout")) {
        ksnprintf(why_f, sizeof why_f, "skipped: the V3D did not answer");    /* no second wait for nothing */
        fade_ok = 0;
    } else {
        fade_ok = probe_fade();
        if (!fade_ok)
            qpu_stats.probe_bad++;
    }
    ksnprintf(status, sizeof status, "particles %s%s%s, fade %s%s%s", particles_ok ? "yes" : "no",
              particles_ok ? "" : ": ", particles_ok ? "" : why_p, fade_ok ? "yes" : "no", fade_ok ? "" : ": ",
              fade_ok ? "" : why_f);
    return particles_ok || fade_ok ? 0 : -1;
}
