/* doom_input.c: host harness for the input path and the speed of a SNES ROM (Doom). Built with -DSNES_TRACE.
 *   gcc -O2 -DSNES_TRACE -Icore/src/bm -o doom_input tests/snes/doom_input.c core/src/bm/snes_{bus,gsu,dsp77,cpu,ppu,spc,dsp,sa1,sa1_cpu}.c
 *   doom_input rom frames [start_frame hold_frames padhex]
 * It prints: the host time of each 300 frames (ms a frame), the frame and line of every CPU read of $4218/$4219 (poll log, first 40 after the pad change),
 * and the first frame at which the ROM reads the new pad value (the latency of a key in frames). Reads no ROM data into the repository. */
#include "snes_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned wpc; static long wcount;
static int frame, polls, pad_frame = -1, seen_frame = -1, nlog;
static uint16_t padnow;
static void on_op(uint32_t pc)
{
    if (wpc && pc == wpc) wcount++;
    uint8_t o = snes_read(pc), a = snes_read(pc + 1), b = snes_read(pc + 2);
    if (a == 0x18 && b == 0x42 && (o == 0xAD || o == 0xAE || o == 0xAC || o == 0xCD || o == 0xED || o == 0x2D || o == 0x0D || o == 0x4D)) {
        polls++;
        if (pad_frame >= 0 && nlog++ < 40) printf("poll f%d L%d pc %06X pad %04X auto %04X\n", frame, S.line, pc, padnow, S.joyauto1);
        if (pad_frame >= 0 && seen_frame < 0 && S.joyauto1 == padnow && padnow) seen_frame = frame;
    }
}
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n); fread(d, 1, n, f); fclose(f);
    const char *why; if (snes_load(d, n, &why)) { printf("load: %s\n", why); return 1; }
    int maxf = atoi(argv[2]), start = argc > 3 ? atoi(argv[3]) : -1, hold = argc > 4 ? atoi(argv[4]) : 0;
    uint16_t pad = argc > 5 ? (uint16_t)strtoul(argv[5], 0, 16) : 0;
    if (getenv("WATCHPC")) wpc = (unsigned)strtoul(getenv("WATCHPC"), 0, 16);
    snes_trace.op = on_op;
    clock_t t0 = clock(); int pf = 0;
    for (frame = 0; frame < maxf; frame++) {
        padnow = (frame >= start && frame < start + hold) ? pad : 0;
        if (frame == start) { pad_frame = frame; }
        snes_frame(padnow, 0, (getenv("ALLDRAW") || frame % 4 == 0));
        if (frame % 300 == 299) { clock_t t1 = clock(); printf("frames %d-%d: %.2f ms a frame, polls %d\n", pf, frame, 1000.0 * (t1 - t0) / CLOCKS_PER_SEC / 300, polls); t0 = t1; pf = frame + 1; polls = 0; snes_debug_t g; snes_debug(&g); printf("  instr %u gsu %u  us cpu %u gsu %u ppu %u spc %u\n", g.instr, g.gsu_instr, g.us_cpu, g.us_gsu, g.us_ppu, g.us_spc); }
    }
    if (start >= 0) printf("pad %04X set at frame %d: first frame the ROM reads it: %d\n", pad, start, seen_frame);
    if (wpc) printf("pc %06X run %ld times\n", wpc, wcount);
    return 0;
}
