/* cpu_bench.c: time of the 65C816 interpreter per instruction on a game-like program. The program is random code from a list of common opcodes
 * (loads, stores, ALU, compares, increments, shifts, stack pairs, branches that fall through) on the work RAM, with some reads of the registers. It runs
 * for many frames of master clock. The host is not the Pi: use the ratio of two runs, not the nanoseconds.
 *   cpu_bench           print ns per instruction (the best of 5 runs) and the instructions
 * Original code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "snes_int.h"

static uint64_t rs = 0x1234567ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

#ifdef CPU_BENCH_STEP                                    /* the code before the loop (git show of an old commit): one cpu_step a call */
static int cpu_run(int lim) { int n = 0; while (snes_clk < lim) { S.line_clk = snes_clk; cpu_step(); n++; } return n; }
#endif
#define RUN(limit) cpu_run(limit)

int main(void)
{
    enum { LEN = 0x8000 };
    static const uint8_t imm[] = { 0x09, 0x29, 0x49, 0x69, 0xE9, 0xC9, 0xA9, 0xA2, 0xA0, 0xE0, 0xC0 };
    static const uint8_t dp[] = { 0x05, 0x25, 0x45, 0x65, 0xE5, 0xC5, 0xA5, 0x85, 0xA5, 0xA5, 0x85, 0xE6, 0xC6, 0xA6, 0x86, 0xB5, 0x95, 0x06, 0x46, 0x26, 0x66, 0x64, 0x24, 0xA4, 0x84 };
    static const uint8_t ab[] = { 0x0D, 0x2D, 0x4D, 0x6D, 0xED, 0xCD, 0xAD, 0x8D, 0xAD, 0xAD, 0x8D, 0xBD, 0x9D, 0xB9, 0x99, 0xEE, 0xCE, 0xAE, 0xAC, 0x8E, 0x9C };
    static const uint8_t one[] = { 0xE8, 0xC8, 0xCA, 0x88, 0x18, 0x38, 0xAA, 0xA8, 0x8A, 0x98, 0x0A, 0x4A, 0x2A, 0x6A, 0xEA, 0x1A, 0x3A, 0xBA };
    static const uint8_t br[] = { 0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0 };
    uint8_t *rom = calloc(1, LEN);
    size_t i = 0;
    while (i < LEN - 0x300) {
        const uint32_t r = rnd(), k = r % 100;
        if (k < 14) { rom[i++] = imm[(r >> 8) % sizeof imm]; rom[i++] = (uint8_t)(r >> 16); }
        else if (k < 44) { rom[i++] = dp[(r >> 8) % sizeof dp]; rom[i++] = (uint8_t)(r >> 16); }
        else if (k < 68) { rom[i++] = ab[(r >> 8) % sizeof ab]; rom[i++] = (uint8_t)(r >> 16); rom[i++] = (uint8_t)((r >> 24) & 0x1F); }
        else if (k < 90) rom[i++] = one[(r >> 8) % sizeof one];
        else if (k < 96) { rom[i++] = 0x48; rom[i++] = 0x68; }                               /* PHA, PLA */
        else { rom[i++] = br[(r >> 8) % sizeof br]; rom[i++] = 0; }                          /* a branch to the next opcode */
    }
    rom[i++] = 0x4C; rom[i++] = 0x00; rom[i++] = 0x80;                                    /* JMP $8000 */
    const size_t off = 0x7FC0;
    memcpy(rom + off, "CPU BENCH            ", 21);
    rom[off + 0x15] = 0x20; rom[off + 0x1C] = 0x00; rom[off + 0x1D] = 0x00; rom[off + 0x1E] = 0xFF; rom[off + 0x1F] = 0xFF;
    rom[off + 0x3C] = 0x00; rom[off + 0x3D] = 0x80;
    const char *why = NULL;
    if (snes_load(rom, LEN, &why)) { printf("load failed: %s\n", why); return 1; }
    free(rom);
    double best = 1e30;
    long total = 0;
    for (int run = 0; run < 5; run++) {
        long n = 0;
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int f = 0; f < 3000; f++) {                                                  /* 3000 lines of 1364 clocks per step of the loop: a few hundred frames */
            const int lim = snes_clk + 1364 * 8;
            n += RUN(lim);
            snes_clk -= 1364 * 8;
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        const double ns = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (double)n;
        if (ns < best) best = ns;
        total = n;
    }
    printf("cpu bench: %.2f ns an instruction (%ld instructions a run)\n", best, total);
    return 0;
}
