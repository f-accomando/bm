/*
 * qpu_bench.c: the VPM micro-benchmark of the V3D (docs/progress/snes-gpu.md, "QPU compose through the VPM"). It answers whether the QPUs can
 * compose the SNES lines (priority, windows, colour math) next to the ARM: the fixed cost of a job, a palette lookup (gathers in 512 bytes), the
 * video RAM (gathers in 64 KB), the VPM DMA stores, and how much the ARM slows down while the QPUs store.
 */
#include "qpu_bench.h"
#include "qpu.h"
#include "qpuprog.h"
#include "v3d.h"
#include "lib/printf.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NJOBS       12                  /* one job on each QPU */
#define CHUNK_WORDS 1024                /* the ARM's work during a QPU run: the sum of 4 KB */
#define CHUNKS      512                 /* ... in a 2 MB array (more than the data cache) */
#define STORE_KB    256                 /* the KB each job stores */

enum { P_NOP, P_GATHER, P_STORE, NPROG };
static uint32_t code_bus[NPROG];
static uint32_t *code_mem;
static uint32_t (*uni)[4];

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

static int setup(void)
{
    if (code_mem)
        return 0;
    if (v3d_init() != 0)
        return -1;
    const size_t words = sizeof qp_bench_nop / 4 + sizeof qp_bench_gather / 4 + sizeof qp_bench_store / 4;
    code_mem = qpu_alloc(words * 4 + 64);
    uni = qpu_alloc(sizeof *uni * NJOBS);
    if (!code_mem || !uni)
        return -1;
    const struct { const uint32_t *p; size_t n; } progs[NPROG] = {
        { qp_bench_nop, sizeof qp_bench_nop }, { qp_bench_gather, sizeof qp_bench_gather }, { qp_bench_store, sizeof qp_bench_store } };
    size_t at = 0;
    for (int i = 0; i < NPROG; i++) {
        memcpy(code_mem + at, progs[i].p, progs[i].n);
        code_bus[i] = v3d_bus(code_mem + at);
        at += progs[i].n / 4;
    }
    return 0;
}

/* runs prog on njobs QPUs; uniforms u[j] = { a + j * astep, b, c, d + j * dstep }; the time in us, or 0 */
static uint32_t timed_run(uint32_t (*us)(void), int prog, int njobs, uint32_t a, uint32_t astep, uint32_t b, uint32_t c, uint32_t d,
                          uint32_t dstep, uint32_t nunif, int hook)
{
    v3d_qpu_job_t jobs[NJOBS];
    for (int j = 0; j < njobs; j++) {
        uni[j][0] = a + (uint32_t)j * astep;
        uni[j][1] = b;
        uni[j][2] = c;
        uni[j][3] = d + (uint32_t)j * dstep;
        jobs[j].code = code_bus[prog];
        jobs[j].unif = v3d_bus(uni[j]);
        jobs[j].nunif = nunif;
    }
    v3d_qpu_idle = hook ? chunk : NULL;
    chunk_n = 0;
    const uint32_t t0 = us();
    const int r = v3d_qpu_run(jobs, njobs, 3000000u);
    const uint32_t t = us() - t0;
    v3d_qpu_idle = NULL;
    return r == 0 ? (t ? t : 1) : 0;
}

static unsigned long ns_per(uint32_t t_us, uint32_t fixed_us, unsigned long long count)
{
    if (!count)
        return 0;
    return (unsigned long)((unsigned long long)(t_us > fixed_us ? t_us - fixed_us : 0) * 1000000ull / count);   /* picoseconds */
}

int qpu_vpm_bench(uint32_t (*us)(void), char *line, size_t n)
{
    if (setup() != 0) {
        ksnprintf(line, n, "vpm bench: no QPUs (%s)", v3d_status());
        return -1;
    }
    uint32_t job1 = 0, job12 = 0;
    for (int i = 0; i < 10; i++) {
        job1 += timed_run(us, P_NOP, 1, 0, 0, 0, 0, 0, 0, 0, 0);
        job12 += timed_run(us, P_NOP, NJOBS, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    job1 /= 10; job12 /= 10;
    if (!job1 || !job12) {
        ksnprintf(line, n, "vpm bench: the QPUs did not run (%s)", v3d_qpu_status());
        return -1;
    }
    const uint32_t store_bytes = (uint32_t)NJOBS * STORE_KB * 1024u;
    uint8_t *region = qpu_alloc(store_bytes > 65536 ? store_bytes : 65536);
    uint32_t *armbuf = malloc((size_t)CHUNKS * CHUNK_WORDS * 4);
    if (!region || !armbuf) {
        qpu_free(region);
        free(armbuf);
        ksnprintf(line, n, "vpm bench: no memory");
        return -1;
    }
    for (size_t i = 0; i < 65536 / 4; i++) ((uint32_t *)region)[i] = (uint32_t)i;
    for (size_t i = 0; i < (size_t)CHUNKS * CHUNK_WORDS; i++) armbuf[i] = (uint32_t)i;
    chunk_buf = armbuf;
    const uint32_t base = v3d_bus(region), iters = 2000;
    const uint32_t g512 = timed_run(us, P_GATHER, NJOBS, base, 0, 0x1FCu, iters, 0, 104729u, 4, 0);
    const uint32_t g64k = timed_run(us, P_GATHER, NJOBS, base, 0, 0xFFFCu, iters, 0, 104729u, 4, 0);
    const uint32_t st = timed_run(us, P_STORE, NJOBS, base, STORE_KB * 1024u, STORE_KB, 0, 0, 0, 2, 0);
    /* the ARM during the stores, and alone for the same time */
    const uint32_t ts = timed_run(us, P_STORE, NJOBS, base, STORE_KB * 1024u, STORE_KB, 0, 0, 0, 2, 1);
    uint32_t cs = chunk_n, ca = 0;
    if (ts) {
        chunk_n = 0;
        const uint32_t t0 = us();
        while (us() - t0 < ts)
            chunk();
        ca = chunk_n;
    }
    const unsigned long long words = (unsigned long long)NJOBS * iters * 64;
    const unsigned long mbs = st > job12 ? (unsigned long)(store_bytes / (st - job12)) : 0;     /* bytes per us = MB/s */
    const int slow = ca ? (int)(100 - (unsigned long long)cs * 100 / ca) : -1;
    ksnprintf(line, n, "vpm bench: job %lu us, jobs12 %lu us, gather512 %lu ps/word, gather64k %lu ps/word, gather512 run %lu us, store %lu MB/s (%lu KB), "
              "arm chunks alone %lu with-store %lu, arm-slowdown %d%%",
              (unsigned long)job1, (unsigned long)job12, ns_per(g512, job12, words), ns_per(g64k, job12, words), (unsigned long)g512, mbs,
              (unsigned long)(store_bytes >> 10), (unsigned long)ca, (unsigned long)cs, slow);
    qpu_free(region);
    free(armbuf);
    return g512 && g64k && st ? 0 : -1;
}
