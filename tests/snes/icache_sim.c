/* icache_sim.c: a model of the I-cache of the ARM1176 (16 KB, 4 ways, 32-byte lines) fed with a QEMU "-d exec" trace on stdin.
 *   icache_sim syms.txt mark_addr end_addr [rr|lru|rand]     syms.txt: "arm-none-eabi-nm -S -n" lines
 * It counts from the first fetch of mark_addr (icache_frame_mark) to the fetch of end_addr; each fetch of mark_addr starts a frame.
 * Output: misses a frame by area (the area that owns the missed address, from the symbol names) and the instructions a frame.
 * No prefetch, no branch predictor, no write buffer: compare two builds with it, do not read it as the miss count of the Pi. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define SETS 128
#define WAYS 4
static uint32_t tag[SETS][WAYS];
static uint8_t val[SETS][WAYS], rrp[SETS];
static uint32_t age[SETS][WAYS];
typedef struct { uint32_t a, sz; char name[48]; } sym_t;
static sym_t *sy;
static int nsy;
static long *symmiss; static long unk[4096];
static int cmp(const void *a, const void *b)
{
    uint32_t x = ((const sym_t *)a)->a, y = ((const sym_t *)b)->a;
    return x < y ? -1 : x > y;
}
static const char *areas[] = { "cpu", "bus", "spc", "dsp", "ppu", "other" };
static int area(const char *n)
{
    static const char *t[5][20] = {
        { "snes_cpu_hot", "exec_op", "cpu_", "dec_add", 0 },
        { "raw_", "snes_read", "snes_write", "map_ptr", "vwrite", "vprefetch", "irq_time", "run_dma", "snes_frame", "snes_hdma", "snes_ppu_", "hdma_", "spc_sync", "gsu_sync", "snes_map", "gsu_", "header_score", 0 },
        { "step", "spc_", "getP", "setP", "adc", "alu", "unary", "timers", "wr", "rd", "ipl_sync", "push", "f8", "rel", "f16", "dp16", 0 },
        { "voice_run", "dsp_", "snes_audio", "snes_dsp", 0 },
        { "ppu_", "draw_bg", "compose", "window_mask", "build_list", "make_tables", 0 } };
    for (int k = 0; k < 5; k++)
        for (int i = 0; t[k][i]; i++)
            if (!strncmp(n, t[k][i], strlen(t[k][i]))) return k;
    return 5;
}
int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    FILE *f = fopen(argv[1], "r");
    char ln[512];
    sy = malloc(sizeof(sym_t) * 200000);
    while (fgets(ln, sizeof ln, f)) {
        unsigned a, s;
        char ty, nm[128];
        if (sscanf(ln, "%x %x %c %127s", &a, &s, &ty, nm) == 4 && (ty == 't' || ty == 'T') && nsy < 200000) {
            sy[nsy].a = a; sy[nsy].sz = s; strncpy(sy[nsy].name, nm, 47); sy[nsy].name[47] = 0; nsy++;
        }
    }
    qsort(sy, nsy, sizeof(sym_t), cmp);
    symmiss = calloc(nsy + 1, sizeof(long));
    uint32_t mark = strtoul(argv[2], 0, 16), endp = strtoul(argv[3], 0, 16);
    int pol = 0;
    if (argc > 4) pol = !strcmp(argv[4], "lru") ? 1 : !strcmp(argv[4], "rand") ? 2 : 0;
    FILE *dump = getenv("ICACHE_DUMP") ? fopen(getenv("ICACHE_DUMP"), "wb") : NULL;   /* the trace as runs of sequential instructions: (pc, count) pairs, for icache_layout.c */
    uint32_t run_pc = 0, run_n = 0;
    unsigned seed = 12345;
    long frames = 0, ins = 0, miss[6] = { 0 };
    int on = 0;
    uint32_t last_line = ~0u, hist[8]; unsigned hn = 0;
    while (fgets(ln, sizeof ln, stdin)) {
        if (strncmp(ln, "Trace", 5)) continue;
        char *p = strchr(ln, '[');
        if (!p) continue;
        p = strchr(p, '/');
        if (!p) continue;
        uint32_t pc = strtoul(p + 1, 0, 16);
        hist[(hn++) & 7] = pc;
        if (getenv("ICACHE_PC") && pc == strtoul(getenv("ICACHE_PC"), 0, 16)) fprintf(stderr, "pc hit after %ld instructions\n", ins);
        if (pc == endp && on) { for (int h = 8; h > 0; h--) fprintf(stderr, "  pc %08x\n", hist[(hn - h) & 7]); fprintf(stderr, "icache_sim: end of the trace after %ld instructions\n", ins); break; }
        if (pc == mark) { on = 1; frames++; if (getenv("ICACHE_V")) fprintf(stderr, "mark %ld after %ld instructions\n", frames, ins); }
        if (!on) continue;
        ins++;
        if (dump) {
            if (run_n && pc == run_pc + 4 * run_n) run_n++;
            else { if (run_n) { uint32_t r[2] = { run_pc, run_n }; fwrite(r, 4, 2, dump); } run_pc = pc; run_n = 1; }
        }
        uint32_t line = pc >> 5, set = line & (SETS - 1), tg = line >> 7;
        int hit = -1;
        for (int w = 0; w < WAYS; w++) if (val[set][w] && tag[set][w] == tg) { hit = w; break; }
        if (hit >= 0) { if (pol == 1) age[set][hit] = (uint32_t)ins; continue; }
        int v = -1;
        for (int w = 0; w < WAYS; w++) if (!val[set][w]) { v = w; break; }
        if (v < 0) {
            if (pol == 0) { v = rrp[set]; rrp[set] = (rrp[set] + 1) & 3; }
            else if (pol == 1) { v = 0; for (int w = 1; w < WAYS; w++) if (age[set][w] < age[set][v]) v = w; }
            else { seed = seed * 1664525u + 1013904223u; v = (seed >> 24) & 3; }
        }
        val[set][v] = 1; tag[set][v] = tg; age[set][v] = (uint32_t)ins;
        last_line = line;
        int lo = 0, hi = nsy - 1, k = -1;
        while (lo <= hi) { int m = (lo + hi) / 2; if (sy[m].a <= pc) { k = m; lo = m + 1; } else hi = m - 1; }
        int ai = 5;
        if (k >= 0 && pc < sy[k].a + (sy[k].sz ? sy[k].sz : 0x10000)) { ai = area(sy[k].name); symmiss[k]++; }
        else { unk[(pc >> 8) & 4095]++; }
        miss[ai]++;
    }
    if (dump) { if (run_n) { uint32_t r[2] = { run_pc, run_n }; fwrite(r, 4, 2, dump); } fclose(dump); }
    (void)last_line;
    if (!feof(stdin) && ferror(stdin)) fprintf(stderr, "icache_sim: read error\n");
    if (frames == 0) { puts("no frames"); return 1; }
    long tot = 0;
    for (int i = 0; i < 6; i++) tot += miss[i];
    printf("frames %ld  ins/frame %ld  misses/frame:", frames, ins / frames);
    for (int i = 0; i < 6; i++) printf(" %s %ld", areas[i], miss[i] / frames);
    printf("  total %ld\n", tot / frames);
    if (getenv("ICACHE_TOP")) {
        int top = atoi(getenv("ICACHE_TOP"));
        for (int n = 0; n < top; n++) {
            long best = 0; int bi = -1;
            for (int i = 0; i < nsy; i++) if (symmiss[i] > best) { best = symmiss[i]; bi = i; }
            long ub = 0; int ui = -1;
            for (int i = 0; i < 4096; i++) if (unk[i] > ub) { ub = unk[i]; ui = i; }
            if (ub > best) { printf("  %7ld  (no symbol) at %06x\n", ub / frames, ui << 8); unk[ui] = 0; }
            else if (bi >= 0) { printf("  %7ld  %-24s %6u bytes  %s\n", best / frames, sy[bi].name, sy[bi].sz, areas[area(sy[bi].name)]); symmiss[bi] = 0; }
        }
    }
    return 0;
}
