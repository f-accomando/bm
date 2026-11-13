#include "qpu_snes.h"
#include "qpu.h"
#include "qpuprog_snes.h"
#include "v3d.h"
#include "lib/printf.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

qpu_snes_stats_t qpu_snes_stats;

#define NJOBS 12                        /* QPUs a run is split over */

enum { P_M7, P_NOP, P_GATHER, P_STREAM, NPROG };

static int inited, m7_ok, off, fail_run;
static char status[96] = "not started";
static uint32_t *code_mem;
static uint32_t code_bus[NPROG];
static uint32_t (*uni)[8];
static qpu_snes_buf_t bufs;
static uint16_t *scratch;               /* the lines of a run when dst is not usable directly */

const char *qpu_snes_status(void) { return status; }
int qpu_snes_m7_ok(void) { return m7_ok && !off; }
const qpu_snes_buf_t *qpu_snes_bufs(void) { return bufs.vram ? &bufs : NULL; }

/* ------------------------------------------------------------- Mode 7, ARM version */

void qpu_snes_m7_arm(uint16_t *dst, int lines, int a, int c, int sel, const uint8_t *vram, const uint16_t *lut, const int32_t *tab)
{
    const int rep = (sel >> 6) & 3;
    for (int l = 0; l < lines; l++) {
        const int32_t X = tab[l * 2], Y = tab[l * 2 + 1];
        uint16_t *d = dst + (size_t)l * 256;
        for (int x = 0; x < 256; x++) {
            const int xx = (sel & 1) ? 255 - x : x;
            const int px = (X + a * xx) >> 8, py = (Y + c * xx) >> 8;
            const int outside = (px | py) & ~1023;
            unsigned col;
            if (outside && rep == 2) {
                col = 0;
            } else if (outside && rep == 3) {
                col = vram[(((py & 7) * 8 + (px & 7)) << 1) + 1];
            } else {
                const unsigned t = vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) << 1];
                col = vram[((t * 64 + (unsigned)(py & 7) * 8 + (unsigned)(px & 7)) << 1) + 1];
            }
            d[x] = lut[col];
        }
    }
}

/* ------------------------------------------------------------------ the QPU run */

/* `jobs` (up to NJOBS) of the program p over `lines` lines; 0, or v3d_qpu_run's result */
static int run_m7(uint16_t *dst, int lines, int a, int c, int sel)
{
    v3d_qpu_job_t jobs[NJOBS];
    const int nj = lines < NJOBS ? lines : NJOBS;
    for (int j = 0; j < nj; j++) {
        const int l0 = lines * j / nj, l1 = lines * (j + 1) / nj;
        uint32_t *u = uni[j];
        u[0] = v3d_bus(bufs.vram);
        u[1] = v3d_bus(bufs.lut);
        u[2] = v3d_bus(dst + (size_t)l0 * 256);
        u[3] = v3d_bus(bufs.tab + l0 * 2);
        u[4] = (uint32_t)(int32_t)a;
        u[5] = (uint32_t)(int32_t)c;
        u[6] = (uint32_t)sel & 0xFF;
        u[7] = (uint32_t)(l1 - l0) * 8;
        jobs[j].code = code_bus[P_M7];
        jobs[j].unif = v3d_bus(u);
        jobs[j].nunif = 8;
    }
    return v3d_qpu_run(jobs, nj, 50000u);
}

int qpu_snes_m7(uint16_t *dst, int lines, int a, int c, int sel)
{
    if (lines < 1 || lines > QPU_SNES_LINES)
        return 0;
    qpu_snes_stats.runs++;
    if (m7_ok && !off) {
        uint16_t *out = dst;
        int direct = 1;
#ifdef QPU_SNES_SCRATCH_ALWAYS
        direct = 0;                     /* the V3D emulator reaches only its own arena */
#endif
        if (((uintptr_t)dst & 63) || !direct) {
            out = scratch;
            direct = 0;
        }
        const int r = run_m7(out, lines, a, c, sel);
        if (r == 0) {
            if (!direct)
                memcpy(dst, scratch, (size_t)lines * 512);
            fail_run = 0;
            qpu_snes_stats.on_qpu++;
            return 1;
        }
        if (r != -2) {                  /* -2: a 3D job runs; not a failure */
            qpu_snes_stats.failures++;
            if (++fail_run >= 2)
                off = 1;
        }
    }
    qpu_snes_m7_arm(dst, lines, a, c, sel, bufs.vram, bufs.lut, bufs.tab);
    qpu_snes_stats.on_arm++;
    return 0;
}

/* ------------------------------------------------------------------------ init */

static uint32_t rs = 88172645463325252u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* the set-up of draw_mode7 for line y (the ARM's part) */
static void line_start(int y, int a, int b, int c, int d, int x0, int y0, int hofs, int vofs, int sel, int32_t *X, int32_t *Y)
{
    const int yy = (sel & 2) ? 255 - y : y;
    const int vx = hofs - x0, vy = vofs - y0;
    const int cx = (vx & 0x2000) ? (vx | ~0x3FF) : (vx & 0x3FF);
    const int cy = (vy & 0x2000) ? (vy | ~0x3FF) : (vy & 0x3FF);
    *X = ((a * cx) & ~63) + ((b * cy) & ~63) + ((b * yy) & ~63) + (x0 << 8);
    *Y = ((c * cx) & ~63) + ((d * cy) & ~63) + ((d * yy) & ~63) + (y0 << 8);
}

static int probe_m7(void)
{
    enum { LINES = 24 };
    uint16_t *ref = malloc(LINES * 512), *out = qpu_alloc(LINES * 512);
    int ok = 0;
    if (ref && out) {
        for (int i = 0; i < 65536; i++)
            bufs.vram[i] = (uint8_t)rnd();
        for (int k = 0; k < 256; k++)
            bufs.lut[k] = (uint16_t)(rnd() >> 8);
        const int a = 0x0B5, b = 0x0B5, c = -0x0B5, d = 0x0B5, sel = 0;
        for (int l = 0; l < LINES; l++)
            line_start(l, a, b, c, d, 128, 112, 40, 20, sel, &bufs.tab[l * 2], &bufs.tab[l * 2 + 1]);
        qpu_snes_m7_arm(ref, LINES, a, c, sel, bufs.vram, bufs.lut, bufs.tab);
        memset(out, 0xAB, LINES * 512);
        const int r = run_m7(out, LINES, a, c, sel);
        ok = r == 0 && !memcmp(out, ref, LINES * 512);
        if (r)
            ksnprintf(status, sizeof status, "mode 7: run %s", v3d_qpu_status());
        else if (!ok)
            ksnprintf(status, sizeof status, "mode 7: the probe picture differs");
    }
    free(ref);
    qpu_free(out);
    return ok;
}

int qpu_snes_init(void)
{
    if (inited)
        return m7_ok ? 0 : -1;
    inited = 1;
    if (v3d_init() != 0) {
        ksnprintf(status, sizeof status, "no V3D (%s)", v3d_status());
        return -1;
    }
    const size_t words = sizeof qp_snes_mode7 / 4 + sizeof qp_bench_nop / 4 + sizeof qp_bench_gather / 4 + sizeof qp_bench_stream / 4;
    code_mem = qpu_alloc(words * 4 + 64);
    uni = qpu_alloc(sizeof *uni * V3D_QPU_MAX);
    bufs.vram = qpu_alloc(65536);
    bufs.lut = qpu_alloc(512);
    bufs.tab = qpu_alloc(QPU_SNES_LINES * 8);
    scratch = qpu_alloc(QPU_SNES_LINES * 512);
    if (!code_mem || !uni || !bufs.vram || !bufs.lut || !bufs.tab || !scratch) {
        ksnprintf(status, sizeof status, "out of memory");
        return -1;
    }
    uint32_t *w = code_mem;
    static const struct { const uint32_t *p; size_t n; } prog[NPROG] = {
        { qp_snes_mode7, sizeof qp_snes_mode7 }, { qp_bench_nop, sizeof qp_bench_nop },
        { qp_bench_gather, sizeof qp_bench_gather }, { qp_bench_stream, sizeof qp_bench_stream } };
    for (int i = 0; i < NPROG; i++) {
        memcpy(w, prog[i].p, prog[i].n);
        code_bus[i] = v3d_bus(w);
        w += prog[i].n / 4;
    }
    m7_ok = probe_m7();
    if (m7_ok)
        ksnprintf(status, sizeof status, "mode 7 yes");
    else if (!strncmp(v3d_qpu_status(), "timeout", 7))
        ksnprintf(status, sizeof status, "mode 7 no: the V3D did not answer");
    return m7_ok ? 0 : -1;
}

/* ------------------------------------------------------------------- benchmark */

#define CHUNK_WORDS 1024                /* the ARM's work during a QPU run: the sum of 4 KB */
#define CHUNKS      512                 /* ... in a 2 MB array (more than the data cache) */

static const uint32_t *chunk_buf;
static uint32_t chunk_n, chunk_sum;

static void chunk(void)
{
    const uint32_t *p = chunk_buf + (size_t)(chunk_n++ & (CHUNKS - 1)) * CHUNK_WORDS;
    uint32_t s = 0;
    for (int i = 0; i < CHUNK_WORDS; i++)
        s += p[i];
    chunk_sum += s;
}

/* runs prog on NJOBS QPUs with the uniforms base, mask, iters; the time of the run in us, or 0 */
static uint32_t timed_run(uint32_t (*us)(void), int prog, int njobs, uint32_t base, uint32_t mask, uint32_t iters, int hook)
{
    v3d_qpu_job_t jobs[NJOBS];
    for (int j = 0; j < njobs; j++) {
        uni[j][0] = base;
        uni[j][1] = mask;
        uni[j][2] = iters;
        jobs[j].code = code_bus[prog];
        jobs[j].unif = v3d_bus(uni[j]);
        jobs[j].nunif = prog == P_NOP ? 0 : 3;
    }
    v3d_qpu_idle = hook ? chunk : NULL;
    chunk_n = 0;
    const uint32_t t0 = us();
    const int r = v3d_qpu_run(jobs, njobs, 3000000u);
    const uint32_t t = us() - t0;
    v3d_qpu_idle = NULL;
    return r == 0 ? (t ? t : 1) : 0;
}

static uint32_t avg_run(uint32_t (*us)(void), int prog, int njobs, int reps)
{
    uint32_t sum = 0;
    for (int i = 0; i < reps; i++) {
        const uint32_t t = timed_run(us, prog, njobs, 0, 0, 0, 0);
        if (!t)
            return 0;
        sum += t;
    }
    return sum / (uint32_t)reps;
}

static unsigned long ns_per(uint32_t t_us, uint32_t fixed_us, unsigned long long count)
{
    if (!count)
        return 0;
    return (unsigned long)((unsigned long long)(t_us > fixed_us ? t_us - fixed_us : 0) * 1000ull / count);
}

int qpu_snes_bench(uint32_t (*us)(void), uint32_t iters, uint32_t big, char *line, size_t n)
{
    if (qpu_snes_init() != 0 && !code_mem) {
        ksnprintf(line, n, "snes gpu bench: no QPUs (%s)", status);
        return -1;
    }
    if (!iters)
        iters = 2000;
    if (!big)
        big = 8u << 20;
    uint32_t job1 = avg_run(us, P_NOP, 1, 20), job12 = avg_run(us, P_NOP, NJOBS, 20);
    if (!job1 || !job12) {
        ksnprintf(line, n, "snes gpu bench: the QPUs did not run (%s)", v3d_qpu_status());
        return -1;
    }
    /* the copy of the 64 KB of video RAM for the V3D, as the PPU does it for a frame */
    static uint8_t src_vram[65536];
    uint32_t copy = 0;
    for (int i = 0; i < 20; i++) {
        const uint32_t t0 = us();
        memcpy(bufs.vram, src_vram, 65536);
        copy += us() - t0;
        src_vram[i] ^= 1;
    }
    copy /= 20;
    uint8_t *region = qpu_alloc(big);
    uint32_t *armbuf = malloc((size_t)CHUNKS * CHUNK_WORDS * 4);
    uint32_t g64 = 0, gbig = 0, st = 0, ca = 0, cg = 0, cs = 0, ta = 0;
    if (region && armbuf) {
        for (size_t i = 0; i < (size_t)big / 4; i += 1024)
            ((uint32_t *)region)[i] = (uint32_t)i;
        for (size_t i = 0; i < (size_t)CHUNKS * CHUNK_WORDS; i++)
            armbuf[i] = (uint32_t)i;
        chunk_buf = armbuf;
        const uint32_t base = v3d_bus(region);
        g64 = timed_run(us, P_GATHER, NJOBS, base, 0xFFFCu, iters, 0);
        gbig = timed_run(us, P_GATHER, NJOBS, base, (big - 1) & ~3u, iters, 0);
        st = timed_run(us, P_STREAM, NJOBS, base, big - 1, iters, 0);
        /* the ARM next to the QPUs: chunks of ARM work done in the wait, with the QPUs busy on the 64 KB (the L2 cache),
         * on SDRAM, and with no QPU job (the same time as the SDRAM run) */
        const uint32_t tg = timed_run(us, P_GATHER, NJOBS, base, 0xFFFCu, iters * 4, 1);
        cg = chunk_n;
        ta = timed_run(us, P_STREAM, NJOBS, base, big - 1, iters, 1);
        cs = chunk_n;
        if (ta) {
            chunk_n = 0;
            const uint32_t t0 = us();
            while (us() - t0 < ta)
                chunk();
            ca = chunk_n;
        }
        /* chunks in a ms */
        cg = tg ? (uint32_t)((unsigned long long)cg * 1000 / tg) : 0;
        cs = ta ? (uint32_t)((unsigned long long)cs * 1000 / ta) : 0;
        ca = ta ? (uint32_t)((unsigned long long)ca * 1000 / ta) : 0;
        if (!g64 || !gbig || !st)
            ksnprintf(status, sizeof status, "bench run: %s", v3d_qpu_status());
    }
    const unsigned long long words = (unsigned long long)NJOBS * iters * 64;
    const unsigned long long bytes = (unsigned long long)NJOBS * iters * 256;
    const unsigned long mbs = st > job12 ? (unsigned long)(bytes / (st - job12)) : 0;     /* bytes per us = MB/s */
    const int slow = ca ? (int)(100 - (unsigned long long)cs * 100 / ca) : -1;
    ksnprintf(line, n,
              "snes gpu bench: job %lu us, jobs12 %lu us, copy64k %lu us, gather64k %lu ns/word, gather%luM %lu ns/word, "
              "stream %lu MB/s, arm chunks/ms alone %lu qpu-l2 %lu qpu-sdram %lu, arm-slowdown %d%%",
              (unsigned long)job1, (unsigned long)job12, (unsigned long)copy, ns_per(g64, job12, words),
              (unsigned long)(big >> 20), ns_per(gbig, job12, words), mbs, (unsigned long)ca, (unsigned long)cg, (unsigned long)cs, slow);
    if (!region || !armbuf) {
        const size_t l = strlen(line);
        ksnprintf(line + l, n - l, " (no memory for the SDRAM tests)");
    }
    qpu_free(region);
    free(armbuf);
    return g64 && gbig && st ? 0 : -1;
}
