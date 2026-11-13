/* icache_layout.c: moves the hot SNES sections in a recorded instruction trace and counts the misses of the 16 KB, 4-way, 32-byte-line cache
 * again, so that an order of the sections (core/src/bm/snes_hot.ld) can be searched without a new QEMU run (a run takes minutes, this takes a second).
 *   icache_layout units.txt hot.txt out.ld trace1.bin [trace2.bin ...]
 * units.txt: core/scripts/icache_units.py of the map of the build that made the traces. hot.txt: the sections to move, one "objfile section" a line,
 * in the order to start from (the lines of snes_hot.ld). trace*.bin: ICACHE_DUMP of tests/snes/icache_sim.c.
 * Environment: ITERS (annealing steps, default 0 = only report), MAXRUNS (runs of each trace to use), SEED.
 * The model: sections are placed one after the other from an address that is a multiple of 4 KB, each shifted by pad words (0 to 63);
 * the annealing swaps sections and changes pads. The result is written as a linker script fragment. Same limits as icache_sim.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#define SETS 128
#define WAYS 4
typedef struct { uint32_t addr, size; char f[64], s[96]; } unit_t;
typedef struct { int u; uint32_t off, n; } run_t;
typedef struct { run_t *r; long n; } trace_t;
static unit_t *U; static int nU;
static trace_t T[8]; static int nT;
static int *hot, nhot;                       /* unit index of each hot section, in the current order */
static int pad[256];                         /* pad words before the section at that position */
static uint32_t *base;                       /* new address of each unit (0 = not moved) */
static int *is_hot;

static uint32_t tagv[SETS][WAYS]; static uint8_t valid[SETS][WAYS], rr[SETS];
static long eval_trace(const trace_t *t)
{
    memset(valid, 0, sizeof valid); memset(rr, 0, sizeof rr);
    long miss = 0; uint32_t last = ~0u;
    for (long i = 0; i < t->n; i++) {
        const run_t *r = &t->r[i];
        uint32_t a = (r->u >= 0 && base[r->u] ? base[r->u] : (r->u >= 0 ? U[r->u].addr : 0)) + r->off;
        if (r->u < 0) a = r->off;
        for (uint32_t line = a >> 5, le = (a + 4 * (r->n - 1)) >> 5; line <= le; line++) {
            if (line == last) continue;
            last = line;
            const uint32_t set = line & (SETS - 1), tg = line >> 7;
            int hit = 0;
            for (int w = 0; w < WAYS; w++) if (valid[set][w] && tagv[set][w] == tg) { hit = 1; break; }
            if (hit) continue;
            miss++;
            int v = -1;
            for (int w = 0; w < WAYS; w++) if (!valid[set][w]) { v = w; break; }
            if (v < 0) { v = rr[set]; rr[set] = (rr[set] + 1) & 3; }
            valid[set][v] = 1; tagv[set][v] = tg;
        }
    }
    return miss;
}
static void place(void)
{
    uint32_t a = 0x4000000;                  /* a multiple of 4 KB, away from every real address */
    for (int i = 0; i < nhot; i++) {
        a += 4u * (uint32_t)pad[i];
        a = (a + 3) & ~3u;
        base[hot[i]] = a;
        a += U[hot[i]].size;
    }
}
static long eval_all(void)
{
    place();
    long m = 0;
    for (int t = 0; t < nT; t++) m += eval_trace(&T[t]);
    return m;
}
static int find_unit(const char *f, const char *s)
{
    for (int i = 0; i < nU; i++) if (!strcmp(U[i].f, f) && !strcmp(U[i].s, s)) return i;
    return -1;
}
static int unit_of(uint32_t pc)
{
    int lo = 0, hi = nU - 1, k = -1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (U[m].addr <= pc) { k = m; lo = m + 1; } else hi = m - 1; }
    return k >= 0 && pc < U[k].addr + U[k].size ? k : -1;
}
static void emit(const char *path)
{
    FILE *o = fopen(path, "w");
    fprintf(o, "/* made by tests/snes/icache_layout.c: the order and the shifts that gave the fewest misses on the traces (%d sections) */\n. = ALIGN(4096);\n__snes_hot_start = .;\n", nhot);
    for (int i = 0; i < nhot; i++) {
        if (pad[i]) fprintf(o, ". = . + %d;\n", pad[i] * 4);
        fprintf(o, "*%s%s(%s)\n", strncmp(U[hot[i]].f, "libc_a-", 7) ? "" : "libc.a:", U[hot[i]].f, U[hot[i]].s);
    }
    fprintf(o, ". = ALIGN(32);\n__snes_hot_end = .;\n");
    fclose(o);
}
int main(int argc, char **argv)
{
    if (argc < 5) return 1;
    FILE *f = fopen(argv[1], "r"); char ln[512];
    U = malloc(sizeof(unit_t) * 100000);
    while (fgets(ln, sizeof ln, f)) {
        unsigned a, s; char fb[64], sb[96];
        if (sscanf(ln, "%x %x %63s %95s", &a, &s, fb, sb) == 4 && nU < 100000) { U[nU].addr = a; U[nU].size = s; strcpy(U[nU].f, fb); strcpy(U[nU].s, sb); nU++; }
    }
    base = calloc(nU, 4); is_hot = calloc(nU, sizeof(int));
    hot = malloc(sizeof(int) * 256);
    f = fopen(argv[2], "r");
    while (fgets(ln, sizeof ln, f)) {
        char fb[64], sb[96];
        if (sscanf(ln, "%63s %95s", fb, sb) == 2) {
            int k = find_unit(fb, sb);
            if (k < 0) { fprintf(stderr, "no such section: %s %s\n", fb, sb); continue; }
            hot[nhot++] = k; is_hot[k] = 1;
        }
    }
    const long maxruns = getenv("MAXRUNS") ? atol(getenv("MAXRUNS")) : 1L << 40;
    for (int t = 4; t < argc && nT < 8; t++) {
        FILE *tf = fopen(argv[t], "rb");
        if (!tf) { fprintf(stderr, "cannot read %s\n", argv[t]); return 1; }
        fseek(tf, 0, SEEK_END); long sz = ftell(tf); fseek(tf, 0, SEEK_SET);
        long n = sz / 8; if (n > maxruns) n = maxruns;
        uint32_t *raw = malloc((size_t)n * 8);
        if (fread(raw, 8, (size_t)n, tf) != (size_t)n) return 1;
        fclose(tf);
        T[nT].r = malloc(sizeof(run_t) * (size_t)n); T[nT].n = n;
        for (long i = 0; i < n; i++) {
            const int u = unit_of(raw[2 * i]);
            T[nT].r[i].u = u; T[nT].r[i].n = raw[2 * i + 1];
            T[nT].r[i].off = u >= 0 ? raw[2 * i] - U[u].addr : raw[2 * i];
        }
        free(raw); nT++;
    }
    if (getenv("LIST")) {                      /* LIST=1: the executed sections, with the instructions run in them (all traces) */
        long *cnt = calloc(nU, sizeof(long));
        for (int t = 0; t < nT; t++) for (long i = 0; i < T[t].n; i++) if (T[t].r[i].u >= 0) cnt[T[t].r[i].u] += T[t].r[i].n;
        for (int i = 0; i < nU; i++) if (cnt[i]) printf("%ld %u %s %s\n", cnt[i], U[i].size, U[i].f, U[i].s);
        return 0;
    }
    /* the real layout (no section moved), then the order of the list */
    long m0 = 0;
    for (int t = 0; t < nT; t++) m0 += eval_trace(&T[t]);
    long bytes = 0; for (int i = 0; i < nhot; i++) bytes += U[hot[i]].size;
    printf("%d traces, %d hot sections, %ld bytes\nas linked          : %ld misses\n", nT, nhot, bytes, m0);
    long cur = eval_all();
    printf("in the list order  : %ld misses\n", cur);
    int iters = getenv("ITERS") ? atoi(getenv("ITERS")) : 0;
    srand(getenv("SEED") ? atoi(getenv("SEED")) : 1);
    long best = cur; int bhot[256], bpad[256]; memcpy(bhot, hot, sizeof(int) * nhot); memcpy(bpad, pad, sizeof(int) * nhot);
    double temp0 = (double)cur * 0.01;
    for (int it = 0; it < iters; it++) {
        const double temp = temp0 * (1.0 - (double)it / iters) + 1.0;
        int sw_a = -1, sw_b = -1, pi = -1, pold = 0;
        if (rand() % 3) { sw_a = rand() % nhot; sw_b = rand() % nhot; if (sw_a == sw_b) continue; int t = hot[sw_a]; hot[sw_a] = hot[sw_b]; hot[sw_b] = t; }
        else { pi = rand() % nhot; pold = pad[pi]; pad[pi] = rand() % 64; }
        const long m = eval_all();
        if (m <= cur || exp((double)(cur - m) / temp) > (double)rand() / RAND_MAX) {
            cur = m;
            if (m < best) { best = m; memcpy(bhot, hot, sizeof(int) * nhot); memcpy(bpad, pad, sizeof(int) * nhot); }
        } else if (sw_a >= 0) { int t = hot[sw_a]; hot[sw_a] = hot[sw_b]; hot[sw_b] = t; }
        else pad[pi] = pold;
        if (it % 200 == 0) { fprintf(stderr, "step %d: current %ld best %ld\n", it, cur, best); }
    }
    memcpy(hot, bhot, sizeof(int) * nhot); memcpy(pad, bpad, sizeof(int) * nhot);
    if (iters) { printf("after the search   : %ld misses\n", eval_all()); emit(argv[3]); }
    return 0;
}
