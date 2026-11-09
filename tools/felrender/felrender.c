/* SPDX-License-Identifier: MIT
 * felrender: a PC program for bm. It plays one note of one preset of Felucca-Engines and writes a WAV file.
 *
 * bm uses it to render the samples of bm Tape (scripts/felpack.py). The kernel does not link it.
 *
 * Origin: the host part (the voice start, the block loop, the call of the engine) follows tests/fe_host.c of
 * Felucca-Engines (MIT, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments), commit
 * d0260074ce100a47675814b7f38d61f11176c926. The envelope, the tail detection and the WAV writer are original
 * bm code.
 *
 * Use:   felrender ENGINE PRESET NOTE HELD_MS OUT.wav     one note: PRESET is a number or an exact name
 *        felrender -l                                      list "ENGINE NUMBER NAME" of every preset
 *   ENGINE: analog lofi trio wheel noise phase
 *   NOTE:   MIDI number (60 = C4)
 *   HELD_MS: time from the key down to the key up
 * Build:  cc -O2 -std=c99 -I<dir of fe_tables.h> -I third_party/felucca-engines/src -o felrender felrender.c
 *         (fe_tables.h: tools/gen_engine_tables.py --fs 32000 fe_tables.h)
 *
 * The output: 16-bit mono WAV at FS Hz, the peak scaled to 32000 (scripts/felpack.py sets the final level).
 * The note plays until the key up, then until the output has stayed under -60 dB of its peak for 40 ms, or until
 * 4 s after the key down. The envelope is a bm choice: the preset bytes ATK DEC SUS REL (0..127) become
 *   attack  1 + 1999 * (ATK/127)^2 ms   a straight ramp
 *   decay   10 + 3990 * (DEC/127)^2 ms  the time to come within -60 dB of the sustain level
 *   sustain SUS/127
 *   release 10 + 1490 * (REL/127)^2 ms  the time to fall by -60 dB
 * All arithmetic is integer: the same input gives the same bytes on every PC. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVOICE 8
#define NPART 4
#include "fe_tables.h"
#include "felucca_engine.h"
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
} track_t;
static track_t trk[NPART];

#include "dsp.c"
#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_noise.c"

static const struct { const char *id; const engine_t *e; } ENGS[] = {
    {"analog", &ENG_ANALOG}, {"lofi", &ENG_LOFI}, {"trio", &ENG_TRIO}, {"wheel", &ENG_WHEEL}, {"noise", &ENG_NOISE},
    {"phase", &ENG_PHASE},
};

#define MAX_MS 4000u
#define FULL (1 << 24)
#define QUIET_MS 40u

/* the envelope of the host: times in control blocks */
typedef struct { int32_t atk, tau_d, sus, tau_r; } env_t;

static uint32_t ms_to_blocks(uint32_t ms) { return (uint32_t)(((uint64_t)ms * FS + 1000u * CTL / 2u) / (1000u * CTL)); }

static env_t env_make(const preset_t *pr)
{
    env_t e;
    uint32_t a = pr->env[0] > 127 ? 127u : pr->env[0], d = pr->env[1] > 127 ? 127u : pr->env[1];
    uint32_t s = pr->env[2] > 127 ? 127u : pr->env[2], r = pr->env[3] > 127 ? 127u : pr->env[3];
    uint32_t ab = ms_to_blocks(1u + 1999u * a * a / (127u * 127u));
    uint32_t db = ms_to_blocks(10u + 3990u * d * d / (127u * 127u));
    uint32_t rb = ms_to_blocks(10u + 1490u * r * r / (127u * 127u));
    e.atk = (int32_t)(ab ? ab : 1u);
    e.tau_d = (int32_t)(db * 10u / 69u);                /* 6.9 time constants are -60 dB */
    e.tau_r = (int32_t)(rb * 10u / 69u);
    if (e.tau_d < 1) e.tau_d = 1;
    if (e.tau_r < 1) e.tau_r = 1;
    e.sus = (int32_t)((int64_t)FULL * s / 127);
    return e;
}

static int32_t env_tick(voice_t *v, const env_t *e)
{
    if (!v->active)
        return 0;
    switch (v->stage) {
    case 1:
        v->env += FULL / e->atk;
        if (v->env >= FULL) {
            v->env = FULL;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += (e->sus - v->env) / e->tau_d;
        if (v->env < FULL / 1000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 3:
        v->env -= v->env / e->tau_r;
        if (v->env < FULL / 1000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env >> 9;                                 /* Q15 */
}

static void voice_on(track_t *t, const engine_t *e, voice_t *v, uint32_t note)
{
    memset(v, 0, sizeof *v);
    v->note = (uint8_t)note;
    v->vel = 100;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = 1;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    e->note_on(t, v);
}

static void put16(FILE *f, uint32_t x) { fputc((int)(x & 255), f); fputc((int)(x >> 8 & 255), f); }
static void put32(FILE *f, uint32_t x) { put16(f, x & 65535u); put16(f, x >> 16); }

static int write_wav(const char *path, const int16_t *s, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36u + n * 2u);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);                                        /* PCM */
    put16(f, 1);                                        /* mono */
    put32(f, FS);
    put32(f, FS * 2u);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, n * 2u);
    for (i = 0; i < n; i++)
        put16(f, (uint16_t)s[i]);
    return fclose(f);
}

int main(int argc, char **argv)
{
    static int32_t buf[(MAX_MS * FS / 1000u) + CTL * 4u];
    static int16_t pcm[sizeof buf / sizeof buf[0]];
    const engine_t *e = 0;
    const preset_t *pr = 0;
    track_t *t = &trk[0];
    env_t env;
    uint32_t n, j, k, i, b, nblk, held, total = 0, note, quiet = 0, qneed;
    int32_t peak = 0, last = 0;
    char *end;

    if (argc == 2 && !strcmp(argv[1], "-l")) {
        for (n = 0; n < NELEM(ENGS); n++)
            for (j = 0; j < ENGS[n].e->npresets; j++)
                printf("%s %u %s\n", ENGS[n].id, j, ENGS[n].e->presets[j].name);
        return 0;
    }
    if (argc != 6) {
        fprintf(stderr, "use: felrender ENGINE PRESET NOTE HELD_MS OUT.wav | felrender -l\n");
        return 2;
    }
    for (n = 0; n < NELEM(ENGS); n++)
        if (!strcmp(argv[1], ENGS[n].id))
            e = ENGS[n].e;
    if (!e) {
        fprintf(stderr, "felrender: no engine %s\n", argv[1]);
        return 2;
    }
    j = (uint32_t)strtoul(argv[2], &end, 10);
    if (*end == 0 && end != argv[2] && j < e->npresets) {
        pr = &e->presets[j];
    } else {
        for (j = 0; j < e->npresets; j++)
            if (!strcmp(argv[2], e->presets[j].name))
                pr = &e->presets[j];
    }
    if (!pr) {
        fprintf(stderr, "felrender: no preset %s in %s\n", argv[2], argv[1]);
        return 2;
    }
    note = (uint32_t)strtoul(argv[3], 0, 10);
    held = (uint32_t)strtoul(argv[4], 0, 10);
    if (note > 127u || held == 0 || held > MAX_MS) {
        fprintf(stderr, "felrender: note 0..127, held 1..%u ms\n", MAX_MS);
        return 2;
    }
    memset(t, 0, sizeof *t);
    memset(drw_t, 0, sizeof drw_t);
    memset(drw_v, 0, sizeof drw_v);
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = pr->e[k];
    t->p[P_ATK] = pr->env[0];
    t->p[P_REL] = pr->env[3];
    t->p[P_FTYPE] = 0;
    env = env_make(pr);
    nblk = (uint32_t)((uint64_t)MAX_MS * FS / (1000u * CTL));
    held = ms_to_blocks(held);
    qneed = ms_to_blocks(QUIET_MS);
    if (qneed < 1)
        qneed = 1;

    voice_on(t, e, &t->v[0], note);
    for (b = 0; b < nblk; b++) {
        int32_t out[CTL];
        voice_t *v = &t->v[0];
        int32_t blockpeak = 0;
        if (b == held && v->active) {
            v->gate = 0;
            v->stage = 3;
        }
        if (e->block)
            e->block(t);
        memset(out, 0, sizeof out);
        if (v->active) {
            vmod_t m;
            int32_t a = env_tick(v, &env);
            uint32_t base;
            if (e->amp)
                a = e->amp(t, v, a);
            if (v->active) {
                base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
                memset(&m, 0, sizeof m);
                m.pitch16 = v->pitch16;
                m.inc = base;
                m.amp0 = last;
                m.amp1 = a;
                m.envq15 = a;
                m.cutoff = (pr->fenv * a) >> 7;         /* the preset's ENV -> FILTER amount */
                m.shape = 64 << 8;
                last = a;
                e->render(t, v, out, CTL, &m);
            }
        }
        for (i = 0; i < CTL; i++) {
            int32_t x = out[i], ax = x < 0 ? -x : x;
            buf[total++] = x;
            if (ax > blockpeak)
                blockpeak = ax;
            if (ax > peak)
                peak = ax;
        }
        if (b >= held && blockpeak < peak / 1000) {
            if (++quiet >= qneed)
                break;
        } else {
            quiet = 0;
        }
    }
    if (peak == 0) {
        fprintf(stderr, "felrender: %s %s: silent\n", argv[1], argv[2]);
        return 1;
    }
    for (i = 0; i < total; i++) {
        int64_t x = (int64_t)buf[i] * 32000 / peak;
        pcm[i] = (int16_t)x;
    }
    if (write_wav(argv[5], pcm, total)) {
        fprintf(stderr, "felrender: cannot write %s\n", argv[5]);
        return 1;
    }
    return 0;
}
