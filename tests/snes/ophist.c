/* ophist.c: a host program that plays a ROM and counts the 65816 opcodes (build with -DSNES_TRACE and the C CPU loop):
 *   gcc -O2 -DSNES_TRACE -DSNES_CPU_C -Icore/src/bm -o ophist tests/snes/ophist.c core/src/bm/snes_{bus,sa1,sa1_cpu,gsu,dsp77,cpu,ppu,spc,dsp}.c -lm
 *   ophist rom frames padscript     (padscript: lines "first last padhex")
 * It prints the opcodes by count. Used to choose what the assembly loop handles (docs/progress/snes-cpu.md, "I-cache work"). The ROM is read from your disk. */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long hist[256], total;
static void on_op(uint32_t pc) { hist[snes_peek8(pc)]++; total++; }
static uint8_t *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *d = malloc(*n); if (fread(d, 1, *n, f) != (size_t)*n) return 0; fclose(f); return d; }
int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    long n; uint8_t *d = slurp(argv[1], &n); const char *why;
    if (!d || snes_load(d, n, &why)) { puts("load failed"); return 1; }
    const int frames = atoi(argv[2]), count_from = frames - 60;
    int f0[256], f1[256], pd[256], ns = 0; char ln[128];
    FILE *sc = fopen(argv[3], "r");
    while (sc && fgets(ln, 128, sc)) { int a, b, c; if (sscanf(ln, "%d %d %x", &a, &b, &c) == 3 && ns < 256) { f0[ns] = a; f1[ns] = b; pd[ns++] = c; } }
    for (int fr = 0; fr < frames; fr++) {
        int p = 0; for (int i = 0; i < ns; i++) if (fr >= f0[i] && fr <= f1[i]) p |= pd[i];
        snes_trace.op = fr >= count_from ? on_op : NULL;
        snes_frame((uint16_t)p, 0, 1);
    }
    for (int k = 0; k < 40; k++) {
        int b = 0; for (int i = 1; i < 256; i++) if (hist[i] > hist[b]) b = i;
        if (!hist[b]) break;
        printf("%02X %6.2f%%  %lu\n", b, 100.0 * hist[b] / total, hist[b]); hist[b] = 0;
    }
    return 0;
}
