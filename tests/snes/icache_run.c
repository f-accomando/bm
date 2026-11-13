/* icache_run.c: plays a ROM on the ARM1176 in QEMU and marks the frames, so that scripts/icache_sim.py can trace the instruction fetches of a few frames
 * and count the misses of a 16 KB, 4-way, 32-byte-line cache (the I-cache of the Pi Zero W). See docs/progress/snes-cpu.md, "I-cache work".
 *   build with -DIC_ROM="\"file\"" -DIC_FW (file or "-") -DIC_WARM (string) -DIC_TRACE (string) -DIC_SCRIPT (file; lines "first last padhex", as in game_run.c)
 * It prints ICACHE_WAIT after the warm-up and reads one byte from the console: the driver switches the trace on and then sends the byte.
 * The ROM and the firmware are read from the user's disk; nothing of them goes into the repository. */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
__attribute__((noinline)) void icache_frame_mark(void) { __asm__ volatile("nop"); }
__attribute__((noinline)) void icache_trace_end(void) { __asm__ volatile("nop; nop"); }
static uint32_t hsum = 2166136261u;
static void hmix(const void *d, unsigned n) { const uint8_t *p = d; while (n--) hsum = (hsum ^ *p++) * 16777619u; }
static uint8_t *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *d = malloc(*n); if (fread(d, 1, *n, f) != (size_t)*n) return 0; fclose(f); return d; }
int main(void)
{
    char *argv[] = { "icache_run", IC_ROM, IC_FW, IC_WARM, IC_TRACE, IC_SCRIPT };
    long n; uint8_t *d = slurp(argv[1], &n); const char *why;
    if (!d || snes_load(d, n, &why)) { printf("load failed\n"); return 1; }
    if (strcmp(argv[2], "-")) { long m; uint8_t *fw = slurp(argv[2], &m); if (fw) snes_dsp_firmware(fw, m, NULL, 0, &why); }
    int warm = atoi(argv[3]), tr = atoi(argv[4]);
    int f0[256], f1[256], pd[256], ns = 0; char ln[128];
    FILE *sc = fopen(argv[5], "r");
    while (sc && fgets(ln, 128, sc)) { int a, b, c; if (sscanf(ln, "%d %d %x", &a, &b, &c) == 3 && ns < 256) { f0[ns] = a; f1[ns] = b; pd[ns++] = c; } }
    static float abuf[2048];
    for (int frame = 0; frame < warm + tr; frame++) {
        int p = 0; for (int i = 0; i < ns; i++) if (frame >= f0[i] && frame <= f1[i]) p |= pd[i];
        if (frame == warm) { snes_debug_t dd; snes_debug(&dd); printf("ICACHE_WAIT, 65816 instructions a frame %u, output hash %08x\n", dd.instr, hsum); fflush(stdout); (void)getchar(); }
        if (frame >= warm) { icache_frame_mark(); printf("frame %d\n", frame); fflush(stdout); }
        snes_frame((uint16_t)p, 0, 1);
        snes_audio_mix(abuf, 800, 1.0f); if (frame < warm) { hmix(abuf, sizeof abuf); int ln; const uint16_t *pic = snes_picture(&ln); hmix(pic, 256u * 2u * (unsigned)ln); }   /* the hash covers the warm-up frames only: the traced frames run the core alone */
    }
    icache_trace_end();
    { snes_debug_t dd; snes_debug(&dd); printf("icache_run: done, 65816 instructions a frame %u\n", dd.instr); }
    return 0;
}
