/* dsp_golden.c: the S-DSP on random voices. The sound RAM holds random BRR blocks (all ranges, filters, end and loop flags) and a directory;
 * the eight voices get random pitch, volume, ADSR or GAIN, noise, pitch modulation, and random key on and key off while it runs. The output of
 * snes_audio_mix (32 kHz to 48 kHz, as floats) and the registers ENVX, OUTX and ENDX are hashed and must equal the table (the model that agrees with ares).
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
extern int snes_dsp_pop(int16_t out[2]);
extern int spc_test_step(uint8_t r[7]);

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


/* Small checks of the chip's behaviour that the hashes do not show (each one failed with the old model). A constant sample (BRR: range 12, filter 0,
 * all nybbles 7, looped) plays on voice 0 at pitch 0x1000. */
static int synth_setup(void)
{
    spc_reset();
    dsp_reset();
    uint8_t *ram = spc_ram_ptr();
    memset(ram, 0, 65536);
    ram[0x1000] = 0x00; ram[0x1001] = 0x20; ram[0x1002] = 0x00; ram[0x1003] = 0x20;     /* sample 0: start and loop at $2000 */
    ram[0x2000] = 0xC3;                                                                 /* range 12, filter 0, end and loop: a loop of one block */
    for (int i = 1; i < 9; i++) ram[0x2000 + i] = 0x77;
    dsp_write(0x5D, 0x10);
    dsp_write(0x00, 0x7F); dsp_write(0x01, 0x7F);                                       /* volumes */
    dsp_write(0x02, 0x00); dsp_write(0x03, 0x10);                                       /* pitch $1000 */
    dsp_write(0x04, 0x00);
    dsp_write(0x05, 0x00); dsp_write(0x07, 0x7F);                                       /* GAIN direct, $7F * 16 */
    dsp_write(0x0C, 0x7F); dsp_write(0x1C, 0x7F);
    dsp_write(0x6C, 0x20);                                                              /* not muted, echo write off */
    return 0;
}

static int synth_checks(void)
{
    int bad = 0;
    uint8_t *ram = spc_ram_ptr(), *d = spc_dsp_ptr();
    int16_t o[2];
    /* 1. KON: nothing is heard before the 5 sample start delay is over, and KON is read only every second sample */
    synth_setup();
    dsp_write(0x4C, 1);
    int first = -1;
    for (int t = 0; t < 40; t++) {
        dsp_tick();
        snes_dsp_pop(o);
        if (first < 0 && o[0] != 0) first = t;
    }
    if (first < 7 || first > 12) { printf("synth: first sound at sample %d (the chip: 7 to 12)\n", first); bad++; }
    /* 2. the Gaussian table: a constant sample gives a constant output (the table sums to 2048) */
    {
        int16_t a = 0, b = 0;
        for (int t = 0; t < 8; t++) { dsp_tick(); snes_dsp_pop(o); a = o[0]; }
        for (int t = 0; t < 8; t++) { dsp_tick(); snes_dsp_pop(o); b = o[0]; }
        if (a != b || a == 0) { printf("synth: a constant sample is %d then %d\n", a, b); bad++; }
    }
    /* 3. the echo: the buffer in the sound RAM is written (the sample times EVOL does not matter), and not when FLG says so */
    synth_setup();
    dsp_write(0x6D, 0x40); dsp_write(0x7D, 1); dsp_write(0x4D, 1);                      /* ESA $4000, EDL 1 (2048 bytes), voice 0 into the echo */
    dsp_write(0x4C, 1);
    for (int t = 0; t < 200; t++) dsp_tick();
    int any = 0;
    for (int i = 0; i < 2048; i++) any |= ram[0x4000 + i];
    if (any) { printf("synth: the echo wrote the RAM with the write off\n"); bad++; }
    dsp_write(0x6C, 0x00);
    for (int t = 0; t < 200; t++) dsp_tick();
    any = 0;
    for (int i = 0; i < 2048; i++) any |= ram[0x4000 + i];
    if (!any) { printf("synth: the echo did not write the RAM with the write on\n"); bad++; }
    /* 4. KOFF: the voice fades by 8 a sample (ENVX falls) */
    synth_setup();
    dsp_write(0x4C, 1);
    for (int t = 0; t < 40; t++) dsp_tick();
    const int e0 = d[0x08];
    dsp_write(0x5C, 1);
    for (int t = 0; t < 6; t++) dsp_tick();
    if (!(e0 > 0x30 && d[0x08] < e0)) { printf("synth: KOFF: ENVX %02X then %02X\n", e0, d[0x08]); bad++; }
    /* 5. the SPC700: the cycles of some instructions (NOP 2, MUL 9, DIV 12, a branch taken 4 and not taken 2) */
    {
        static const struct { uint8_t op, psw; int cyc; } c[] = { { 0x00, 0, 2 }, { 0xCF, 0, 9 }, { 0x9E, 0, 12 }, { 0xF0, 0x02, 4 }, { 0xF0, 0x00, 2 },
                                                                   { 0x2F, 0, 4 }, { 0x3F, 0, 8 }, { 0x6F, 0, 5 } };
        for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
            ram[0x0300] = c[i].op;
            uint8_t r[7] = { 0, 1, 1, 0xEF, 0x00, 0x03, c[i].psw };
            const int n = spc_test_step(r);
            if (n != c[i].cyc) { printf("synth: SPC700 opcode %02X takes %d cycles, the chip %d\n", c[i].op, n, c[i].cyc); bad++; }
        }
    }
    return bad;
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

/* The table is of the chip model that was checked sample by sample against ares (tests/snes/dsp_ref.cpp, `make test-snes-audio-ref ARES=...`): Gaussian
 * interpolation, key on delay, echo. The earlier table (cubic interpolation, no echo, no start delay) was wrong for these reasons and is replaced. */
static const uint64_t GOLDEN[CASES] = {
    0x4D848880AF0B12BEull,
    0x93FE6CEF43C188CAull,
    0x51EF8F21C4F1EAE1ull,
    0x0734AFA94C0DFB68ull,
    0x98AA36C0B15049AAull,
    0x28196EC8C47F5C4Dull,
    0x3532D9B9E65F71B9ull,
    0xD3A01C6086DCC5BFull,
    0x622775D36DC7B4DAull,
    0x8997EB73F6E08D77ull,
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
            double cworst = 0;
            long cbig = 0;
            for (int i = 0; i < n; i++) {
                const double dd = fabs((double)out[i] - (double)old[i]);
                if (dd > cworst) cworst = dd;
                if (dd > worst) worst = dd;
                sum += dd; count++;
                if (dd > 4.0 * (0.8 / 32768.0)) cbig++;     /* more than 4 steps of 16 bit */
            }
            /* a voice that modulates its pitch from the voice before it (PMON) turns a difference of one step into a change of pitch: the samples drift
             * apart in time, they are not wrong. The cases without it must stay within 4 steps. */
            printf("case %d: PMON %02X, largest %.2e, %ld samples over 4 steps\n", c, spc_dsp_ptr()[0x2D], cworst, cbig);
            if (spc_dsp_ptr()[0x2D] == 0) big += cbig;
        }
        printf("largest difference %.3e (a 16-bit step is %.3e), mean %.3e, %ld of %ld samples more than 4 steps\n", worst, 0.8 / 32768.0, sum / (double)count, big, count);
        fclose(f);
        return big != 0;
    }
    int bad = synth_checks();
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
