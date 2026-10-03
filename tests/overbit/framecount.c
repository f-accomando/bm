/*
 * framecount.c - the ARM instructions of every frame of a bmhost run under
 * qemu-arm, by function (tests/overbit/frames.py builds and runs it).
 *
 *   qemu-arm -d in_asm,exec,nochain -D FIFO bmhost-arm CART ... &
 *   framecount TABLE MARKER [BLOCKS] < FIFO > CSV
 *
 * TABLE: "addr width cost" (hex addr) for every instruction of the binary,
 * from objdump (width 4 for ARM, 2 or 4 for Thumb; cost: rough ARM1176
 * cycles). MARKER: the address of host_frame, which runs once a frame.
 * CSV: "frame,function,instructions,cycles" for every function that ran in
 * that frame; frame 0 is everything before the first host_frame. BLOCKS:
 * "pc instructions cycles runs function" of every block that ran after
 * frame 0 (the hot spots inside a function).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t addr; uint8_t width, cost; } ins_t;
static ins_t *ins;
static size_t nins;

static int find(uint64_t a)
{
    size_t lo = 0, hi = nins;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (ins[m].addr < a) lo = m + 1; else hi = m;
    }
    return lo < nins && ins[lo].addr == a ? (int)lo : -1;
}

/* the translated blocks: pc -> instructions, cycles, function */
typedef struct { uint64_t pc; uint32_t n, c; int sym; uint64_t runs; } blk_t;
static blk_t *blk;
static size_t nblk_cap = 1 << 20;

static blk_t *slot(uint64_t pc)
{
    size_t h = (size_t)(pc * 0x9E3779B97F4A7C15ull >> 40) & (nblk_cap - 1);
    while (blk[h].pc && blk[h].pc != pc) h = (h + 1) & (nblk_cap - 1);
    return &blk[h];
}

/* function names */
static char **names;
static int nnames, cap_names;

static int intern(const char *s)
{
    static int last = -1;
    if (last >= 0 && !strcmp(names[last], s)) return last;
    for (int i = 0; i < nnames; i++)
        if (!strcmp(names[i], s)) return last = i;
    if (nnames == cap_names) {
        cap_names = cap_names ? cap_names * 2 : 1024;
        names = realloc(names, sizeof *names * (size_t)cap_names);
    }
    names[nnames] = strdup(s);
    return last = nnames++;
}

/* this frame's counts by function */
static uint64_t *fn, *fc;
static int cap_fn;

static void flush(long frame)
{
    for (int i = 0; i < nnames && i < cap_fn; i++)
        if (fn[i]) {
            printf("%ld,%s,%llu,%llu\n", frame, names[i], (unsigned long long)fn[i], (unsigned long long)fc[i]);
            fn[i] = fc[i] = 0;
        }
}

static void grow(int sym)
{
    if (sym < cap_fn) return;
    int n = cap_fn ? cap_fn : 1024;
    while (n <= sym) n *= 2;
    fn = realloc(fn, sizeof *fn * (size_t)n);
    fc = realloc(fc, sizeof *fc * (size_t)n);
    memset(fn + cap_fn, 0, sizeof *fn * (size_t)(n - cap_fn));
    memset(fc + cap_fn, 0, sizeof *fc * (size_t)(n - cap_fn));
    cap_fn = n;
}

static int hexv(int ch)
{
    return ch <= '9' ? ch - '0' : (ch | 32) - 'a' + 10;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "framecount TABLE MARKER < log > csv\n");
        return 2;
    }
    FILE *t = fopen(argv[1], "r");
    if (!t) { perror(argv[1]); return 1; }
    size_t cap = 1 << 16;
    ins = malloc(sizeof *ins * cap);
    unsigned long long a;
    unsigned w, c;
    while (fscanf(t, "%llx %u %u", &a, &w, &c) == 3) {
        if (nins == cap) ins = realloc(ins, sizeof *ins * (cap *= 2));
        ins[nins++] = (ins_t){ a, (uint8_t)w, (uint8_t)c };
    }
    fclose(t);
    const uint64_t marker = strtoull(argv[2], NULL, 16);
    blk = calloc(nblk_cap, sizeof *blk);

    static char line[1 << 16];
    char sym[512] = "";
    uint64_t pc = 0, nbytes = 0;
    int in_block = 0;
    long frame = 0;
    unsigned long long total = 0;

#define CLOSE() do {                                                          \
        if (in_block && pc && nbytes) {                                       \
            blk_t *b = slot(pc);                                              \
            if (b->pc != pc) b->runs = 0;                                     \
            b->pc = pc; b->n = 0; b->c = 0; b->sym = intern(sym);             \
            int k = find(pc);                                                 \
            uint64_t p = pc;                                                  \
            if (k >= 0) {                                                     \
                while (p < pc + nbytes && k >= 0 && (size_t)k < nins &&       \
                       ins[k].addr == p) {                                    \
                    b->n++; b->c += ins[k].cost; p += ins[k].width; k++;      \
                }                                                             \
            }                                                                 \
            if (p < pc + nbytes) { b->n += (uint32_t)((pc + nbytes - p) / 2); \
                                   b->c += (uint32_t)((pc + nbytes - p) / 2); } \
        }                                                                     \
        in_block = 0; pc = 0; nbytes = 0;                                     \
    } while (0)

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'T' && !strncmp(line, "Trace", 5)) {
            CLOSE();
            char *q = strchr(line, '[');
            if (!q) continue;
            q = strchr(q, '/');
            if (!q) continue;
            uint64_t v = 0;
            for (q++; *q != '/' && *q; q++) v = v << 4 | (uint64_t)hexv(*q);
            if (v == marker) {
                flush(frame);
                frame++;
            }
            blk_t *b = slot(v);
            if (b->pc) {
                grow(b->sym);
                fn[b->sym] += b->n;
                fc[b->sym] += b->c;
                total += b->n;
                if (frame > 0) b->runs++;
            }
            continue;
        }
        if (line[0] == 'I' && !strncmp(line, "IN:", 3)) {
            CLOSE();
            char *s = line + 3;
            while (*s == ' ') s++;
            size_t n = strcspn(s, "\r\n");
            if (n == 0) { strcpy(sym, "?"); }
            else { if (n >= sizeof sym) n = sizeof sym - 1; memcpy(sym, s, n); sym[n] = 0; }
            in_block = 1;
            continue;
        }
        if (in_block && line[0] == '0' && line[1] == 'x' && !pc) {
            pc = strtoull(line + 2, NULL, 16);
            continue;
        }
        if (in_block && !strncmp(line, "OBJD-", 5)) {
            char *s = strchr(line, ':');
            if (s) {
                s++;
                while (*s == ' ') s++;
                nbytes += strcspn(s, " \r\n") / 2;
            }
            continue;
        }
    }
    CLOSE();
    flush(frame);
    if (argc > 3) {
        FILE *o = fopen(argv[3], "w");
        if (!o) { perror(argv[3]); return 1; }
        for (size_t i = 0; i < nblk_cap; i++)
            if (blk[i].pc && blk[i].runs)
                fprintf(o, "%llx %u %u %llu %s\n", (unsigned long long)blk[i].pc, blk[i].n, blk[i].c,
                        (unsigned long long)blk[i].runs, names[blk[i].sym]);
        fclose(o);
    }
    fprintf(stderr, "framecount: %ld frames, %llu instructions\n", frame, total);
    return 0;
}
