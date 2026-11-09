/* dsp_golden.c: the S-DSP on random voices. The sound RAM holds random BRR blocks (all ranges, filters, end and loop flags) and a directory;
 * the eight voices get random pitch, volume, ADSR or GAIN, noise, pitch modulation, and random key on and key off while it runs. The output of
 * snes_audio_mix (32 kHz to 48 kHz, as floats) and the registers ENVX, OUTX and ENDX are hashed and must equal the table, made with the code
 * before the speed work.
 *   dsp_golden             check against the table (exact)
 *   dsp_golden print       print the table
 *   dsp_golden diff        the largest difference of a sample against the saved samples of the old code (a file) (see the Makefile)
 *   dsp_golden bench       microseconds for one tick (one stereo sample) with eight voices on
 * Original code. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "snes_int.h"

extern uint8_t *spc_ram_ptr(void);
extern uint8_t *spc_dsp_ptr(void);

static uint64_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static void setup(int c)
{
    rs = 0x2545F4914F6CDD1Dull + (uint64_t)c * 104729u;
    spc_reset();
    dsp_reset();
    uint8_t *ram = spc_ram_ptr(), *d = spc_dsp_ptr();
    for (int i = 0; i < 65536; i++) ram[i] = (uint8_t)rnd();
    /* the sample directory at $1000: 8 entries of start and loop address, at blocks of 9 bytes in $2000..$FFFF */
    for (int i = 0; i < 8; i++) {
        const unsigned s = 0x2000 + (rnd() % 0x1000) * 9, l = 0x2000 + (rnd() % 0x1000) * 9;
        ram[0x1000 + i * 4] = (uint8_t)s; ram[0x1000 + i * 4 + 1] = (uint8_t)(s >> 8);
        ram[0x1000 + i * 4 + 2] = (uint8_t)l; ram[0x1000 + i * 4 + 3] = (uint8_t)(l >> 8);
    }
    /* block headers: every 9th byte from $2000, with a range up to 12 mostly, filters, and an end flag now and then */
    for (unsigned a = 0x2000; a + 9 <= 0x2000 + 0x1000 * 9 + 9 && a < 0xFFF0; a += 9) {
        const unsigned range = (rnd() % 8 == 0) ? 13 + rnd() % 3 : rnd() % 13, filter = rnd() % 4;
        const unsigned end = (rnd() % 6 == 0), loop = (rnd() % 2);
        ram[a] = (uint8_t)((range << 4) | (filter << 2) | (loop << 1) | end);
    }
    for (int i = 0; i < 8; i++) {
        uint8_t *r = &d[i * 16];
        r[0] = (uint8_t)rnd(); r[1] = (uint8_t)rnd();                                    /* volumes, signed */
        const unsigned pitch = (c % 3 == 0) ? 0x1000 + rnd() % 0x400 : rnd() % 0x3FFF;   /* near 32 kHz, or anything */
        r[2] = (uint8_t)pitch; r[3] = (uint8_t)(pitch >> 8);
        r[4] = (uint8_t)(rnd() % 8);                                                     /* the sample number */
        r[5] = (uint8_t)((rnd() % 4 ? 0x80 : 0) | (rnd() & 0x7F));                       /* ADSR mostly, else GAIN */
        r[6] = (uint8_t)rnd();
        r[7] = (uint8_t)rnd();
    }
    d[0x0C] = (uint8_t)(0x40 + rnd() % 0x3F); d[0x1C] = (uint8_t)(0x40 + rnd() % 0x3F);
    d[0x5D] = 0x10;
    d[0x2D] = (uint8_t)rnd();                                                            /* pitch modulation */
    d[0x3D] = (uint8_t)(rnd() % 3 == 0 ? rnd() : 0);                                     /* noise */
    d[0x6C] = (uint8_t)(rnd() & 0x1F);                                                   /* not muted, not reset, noise rate */
}

#define CASES 10
#define TICKS 24000

static uint64_t hash64(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}

/* runs a case; the output samples go to out[] (2 * ticks * 3 / 2 floats) if it is not NULL */
static uint64_t run_case(int c, float *out, int *nout)
{
    setup(c);
    uint8_t *d = spc_dsp_ptr();
    uint64_t h = 0xCBF29CE484222325ull;
    int n = 0;
    for (int t = 0; t < TICKS; t++) {
        if (t % 700 == 0) {
            dsp_write(0x4C, (uint8_t)rnd());                    /* key on */
            if (t % 2100 == 1400) dsp_write(0x5C, (uint8_t)rnd());   /* key off */
            else d[0x5C] = 0;
        }
        dsp_tick();
        if (t % 1000 == 999) {
            float buf[3000];
            memset(buf, 0, sizeof buf);
            snes_audio_mix(buf, 1500, 1.0f);
            h = hash64(h, buf, sizeof buf);
            if (out) { memcpy(out + n, buf, sizeof buf); n += 3000; }
            for (int i = 0; i < 8; i++) { h = hash64(h, &d[i * 16 + 8], 2); }
            h = hash64(h, &d[0x7C], 1);
        }
    }
    if (nout) *nout = n;
    return h;
}

/* The table is of the code with the integer cubic. The float cubic that came before gave the same samples but for a rounding: over 720000 samples of these
 * cases the largest difference was 2 steps of 16 bit (4.9e-05 in the floats of snes_audio_mix), the mean 1.6e-09 (2026-11-09, `dsp_golden diff`). */
static const uint64_t GOLDEN[CASES] = {
    0x48608291F2C43D87ull,
    0x5B5F2D0FE1F8330Bull,
    0x1CB3759F6BD5B18Cull,
    0x958D904051E08FCFull,
    0xECE382611E3DC08Full,
    0x6DC5F1BBA805B766ull,
    0x1E14098E7A48EE71ull,
    0x54E52C8F8FF5BC1Eull,
    0x5CBBEB60FC13E550ull,
    0xFD6FAC1B8FD1CCA4ull,
};

static void bench(void)
{
    setup(1);
    uint8_t *d = spc_dsp_ptr();
    for (int i = 0; i < 8; i++) { d[i * 16 + 5] = 0xFF; d[i * 16 + 6] = 0xE0; }   /* a fast attack and a full sustain: every voice stays on */
    dsp_write(0x4C, 0xFF);
    struct timespec a, b;
    for (int t = 0; t < 2000; t++) dsp_tick();
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int t = 0; t < 200000; t++) { dsp_tick(); if (t % 4000 == 3999) { float buf[3000]; snes_audio_mix(buf, 1500, 1.0f); dsp_write(0x4C, 0xFF); } }
    clock_gettime(CLOCK_MONOTONIC, &b);
    printf("%.3f us a tick (a second of sound is 32000 ticks: %.1f ms)\n", ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 200000 / 1e3,
           ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 200000 / 1e3 * 32);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
    const int print = argc > 1 && !strcmp(argv[1], "print");
    /* save and compare: the samples of the old code in a file, the largest difference of the new code against them */
    if (argc > 2 && !strcmp(argv[1], "save")) {
        FILE *f = fopen(argv[2], "wb");
        for (int c = 0; c < CASES; c++) { static float out[TICKS * 3]; int n; run_case(c, out, &n); fwrite(out, sizeof(float), (size_t)n, f); }
        fclose(f);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "diff")) {
        FILE *f = fopen(argv[2], "rb");
        double worst = 0, sum = 0;
        long count = 0, big = 0;
        for (int c = 0; c < CASES; c++) {
            static float out[TICKS * 3], old[TICKS * 3];
            int n;
            run_case(c, out, &n);
            if (fread(old, sizeof(float), (size_t)n, f) != (size_t)n) { printf("short file\n"); return 1; }
            for (int i = 0; i < n; i++) {
                const double dd = fabs((double)out[i] - (double)old[i]);
                if (dd > worst) worst = dd;
                sum += dd; count++;
                if (dd > 4.0 * (0.8 / 32768.0)) big++;      /* more than 4 steps of 16 bit */
            }
        }
        printf("largest difference %.3e (a 16-bit step is %.3e), mean %.3e, %ld of %ld samples more than 4 steps\n", worst, 0.8 / 32768.0, sum / (double)count, big, count);
        fclose(f);
        return big != 0;
    }
    int bad = 0;
    if (print) printf("static const uint64_t GOLDEN[CASES] = {\n");
    for (int c = 0; c < CASES; c++) {
        const uint64_t h = run_case(c, NULL, NULL);
        if (print) printf("    0x%016llXull,\n", (unsigned long long)h);
        else if (h != GOLDEN[c]) { printf("MISMATCH case %d: %016llX, expected %016llX\n", c, (unsigned long long)h, (unsigned long long)GOLDEN[c]); bad++; }
    }
    if (print) printf("};\n");
    else printf("dsp golden: %d cases, %s\n", CASES, bad ? "FAILED" : "ok");
    return bad != 0;
}
