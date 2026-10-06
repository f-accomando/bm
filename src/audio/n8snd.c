/*
 * nano8 sound (see n8snd.h). The game asks for effects and music from its
 * main loop; the requests go through a small queue and the audio
 * interrupt applies them before it renders, so nothing here needs a lock.
 *
 * The carts' timing: a sound effect's speed counts units of 183 samples
 * at 22050 Hz (1/120.5 s) per note; a note's pitch p sounds at
 * 440 * 2^((p - 33) / 12) Hz; its volume is 0..7. A custom instrument is
 * sound effect 0-7 played under the note, transposed by (p - 24).
 */
#include "n8snd.h"

#include <math.h>
#include <string.h>

#define CHANS       4
#define SFX_BASE    0x3200
#define MUSIC_BASE  0x3100
#define QLEN        64

enum { C_SFX, C_MUSIC, C_ATTACH, C_PAUSE };

typedef struct {
    uint8_t type;
    int16_t a, b, c, d;
    const uint8_t *ram;
} cmd_t;

typedef struct {
    int sfx;                /* -1: idle */
    int first, end;         /* notes [first, end) when not looping */
    int released;
    double t;               /* samples since the sound started */
    int note;               /* the note sounding, -1 before the first */
    float freq, vol;        /* of the note sounding (slides start from them) */
    float pfreq, pvol;      /* the previous note's */
    float phase, phase2;
    uint32_t rng;
    float noise, noise2, lp;
    /* custom instrument under the note */
    int inst;               /* -1: none */
    double it;
    float iphase;
    int inote;
    float ifreq, ivol, ipfreq, ipvol;
} voice_t;

typedef struct {
    voice_t game, music;    /* a game sound covers the music on its channel */
} chan_t;

static const uint8_t *volatile ram;
static uint32_t rate = 48000;
static float unit;                  /* samples per speed unit */
static chan_t ch[CHANS];
static int paused;

/* music */
static int mpat = -1;               /* the pattern playing, -1 none */
static double mt, mlen;             /* samples into it, and its length */
static int mmask, mcount;
static float mfade = 1, mfade_step;   /* per sample */
static int mstop_after_fade;

static volatile cmd_t q[QLEN];
static volatile unsigned qhead, qtail;

static void push(cmd_t c)
{
    unsigned h = qhead, n = (h + 1) % QLEN;
    if (n == qtail)
        return;                     /* full: dropped (the interrupt is not running) */
    q[h].type = c.type; q[h].a = c.a; q[h].b = c.b; q[h].c = c.c; q[h].d = c.d; q[h].ram = c.ram;
    __asm__ volatile("" ::: "memory");
    qhead = n;
}

void n8snd_attach(const uint8_t *r, uint32_t sample_rate)
{
    cmd_t c = { C_ATTACH, 0, 0, 0, 0, r };
    if (sample_rate)
        rate = sample_rate;
    /* letting go is immediate: the RAM may be freed right after. One core,
     * and the interrupt never stops halfway for this code, so a store is
     * enough; the request still resets the channels. */
    if (!r)
        ram = NULL;
    push(c);
}

void n8snd_sfx(int n, int channel, int offset, int length)
{
    cmd_t c = { C_SFX, (int16_t)n, (int16_t)channel, (int16_t)offset, (int16_t)length, NULL };
    push(c);
}

void n8snd_music(int n, int fade_ms, int mask)
{
    cmd_t c = { C_MUSIC, (int16_t)n, (int16_t)(fade_ms > 32767 ? 32767 : fade_ms), (int16_t)mask, 0, NULL };
    push(c);
}

void n8snd_pause(int on)
{
    cmd_t c = { C_PAUSE, (int16_t)on, 0, 0, 0, NULL };
    push(c);
}

/* ------------------------------------------------------------ notes */

typedef struct {
    int pitch, wave, vol, fx, custom;
} note_t;

static note_t get_note(int sfx, int i)
{
    const uint8_t *p = ram + SFX_BASE + sfx * 68 + i * 2;
    unsigned v = p[0] | p[1] << 8;
    note_t n = { (int)(v & 63), (int)(v >> 6 & 7), (int)(v >> 9 & 7), (int)(v >> 12 & 7), (int)(v >> 15 & 1) };
    return n;
}

static int speed_of(int sfx)
{
    int s = ram[SFX_BASE + sfx * 68 + 65];
    return s ? s : 1;
}

/* the notes [first, end) a sound plays, and its loop (ls < le) */
static void bounds(int sfx, int *ls, int *le, int *end)
{
    const uint8_t *d = ram + SFX_BASE + sfx * 68;
    *ls = d[66];
    *le = d[67];
    if (*ls > 32) *ls = 32;
    if (*le > 32) *le = 32;
    *end = (*le == 0 && *ls > 0) ? *ls : 32;      /* end 0: start is the length */
}

/* Hz of the pitches -64..191 (an instrument transposes its notes) */
static float hz_table[256];

static float pitch_hz(float p)
{
    int i = (int)p + 64;
    if (i < 0) i = 0;
    if (i > 255) i = 255;
    if (!hz_table[0])
        for (int k = 0; k < 256; k++)
            hz_table[k] = 440.0f * powf(2.0f, (k - 64 - 33.0f) / 12.0f);
    return hz_table[i];
}

static float tri(float t)
{
    return fabsf(4.0f * t - 2.0f) - 1.0f;
}

static float wave(voice_t *v, int w, float t, float freq)
{
    switch (w) {
    case 0: return tri(t) * 0.5f;
    case 1: return (t < 0.875f ? t / 0.875f * 2.0f - 1.0f : 1.0f - (t - 0.875f) / 0.125f * 2.0f) * 0.5f;
    case 2: return (t < 0.5f ? t : t - 1.0f) * 0.6f;
    case 3: return t < 0.5f ? 0.25f : -0.25f;
    case 4: return t < 0.3125f ? 0.25f : -0.25f;
    case 5: {
        float o = t < 0.5f ? 3.0f - fabsf(24.0f * t - 6.0f) : 1.0f - fabsf(16.0f * t - 12.0f);
        return o / 9.0f * 1.2f;
    }
    case 6: {
        /* noise: a new random level twice a cycle, softened more for low notes */
        (void)freq;
        float k = freq / 1200.0f;
        if (k > 1) k = 1;
        if (k < 0.05f) k = 0.05f;
        v->noise += (v->noise2 - v->noise) * k;
        return v->noise * 0.6f;
    }
    default: {
        float a = tri(t), b = tri(fmodf(t + 0.5f * fabsf(tri(v->phase2)), 1.0f));
        return (a - b) / 3.0f;
    }
    }
}

static void voice_stop(voice_t *v)
{
    v->sfx = -1;
    v->inst = -1;
}

static void voice_start(voice_t *v, int sfx, int offset, int length)
{
    int ls, le, end;
    bounds(sfx, &ls, &le, &end);
    v->sfx = sfx;
    v->first = offset < 0 ? 0 : offset > 31 ? 31 : offset;
    v->end = length > 0 && v->first + length < end ? v->first + length : end;
    v->released = 0;
    v->t = 0;
    v->note = -1;
    v->inst = -1;
    if (!v->rng)
        v->rng = 0x12345u + (uint32_t)sfx;
}

/* the note index of a voice now, -1 when it has ended */
static int note_index(double t, int sfx, int first, int end, int released, double *frac)
{
    int ls, le, e;
    bounds(sfx, &ls, &le, &e);
    double len = speed_of(sfx) * (double)unit;
    double pos = t / len;
    int n = first + (int)pos;
    *frac = pos - floor(pos);
    if (le > ls && !released && n >= le && first < le) {
        n = ls + (n - ls) % (le - ls);
        return n;
    }
    return n < end ? n : -1;
}

/* One sample of a note (or an instrument note) with its effect. */
static float play_note(voice_t *v, int sfx, note_t nt, int index, double frac, int speed, float *phase,
                       float *freq, float *vol, float pfreq, float pvol, float transpose)
{
    float f = pitch_hz((float)nt.pitch + transpose), a = nt.vol / 7.0f;
    float fr = (float)frac;
    switch (nt.fx) {
    case 1:                                 /* slide from the previous note */
        f = pfreq + (f - pfreq) * fr;
        a = pvol + (a - pvol) * fr;
        break;
    case 2: {                               /* vibrato */
        double ts = v->t / (double)rate;
        f *= 1.0f + 0.0144f * tri((float)(ts * 7.0 - floor(ts * 7.0)));    /* a quarter tone */
        break;
    }
    case 3:                                 /* drop */
        f *= 1.0f - fr;
        break;
    case 4:                                 /* fade in */
        a *= fr;
        break;
    case 5:                                 /* fade out */
        a *= 1.0f - fr;
        break;
    case 6: case 7: {                       /* arpeggio over the group of 4 notes */
        int step = nt.fx == 6 ? 4 : 8;
        if (speed <= 8) step /= 2;
        double ticks = (index * (double)speed + frac * speed);
        int k = (int)(ticks / step) & 3;
        note_t g = get_note(sfx, (index & ~3) + k);
        f = pitch_hz((float)g.pitch + transpose);
        break;
    }
    }
    *freq = f;
    *vol = a;
    *phase += f / (float)rate;
    if (*phase >= 1.0f) {
        *phase -= floorf(*phase);
        if (nt.wave == 6 && !nt.custom) {
            v->rng = v->rng * 1103515245u + 12345u;
            v->noise2 = (float)((v->rng >> 16) & 0x7FFF) / 16384.0f - 1.0f;
        }
    }
    if (nt.wave == 6 && *phase >= 0.5f && *phase - f / (float)rate < 0.5f) {
        v->rng = v->rng * 1103515245u + 12345u;
        v->noise2 = (float)((v->rng >> 16) & 0x7FFF) / 16384.0f - 1.0f;
    }
    return wave(v, nt.wave, *phase, f) * a;
}

/* the next sample of a voice */
static float voice_sample(voice_t *v)
{
    if (v->sfx < 0)
        return 0;
    double frac;
    int n = note_index(v->t, v->sfx, v->first, v->end, v->released, &frac);
    if (n < 0) {
        voice_stop(v);
        return 0;
    }
    note_t nt = get_note(v->sfx, n);
    int speed = speed_of(v->sfx);
    if (n != v->note) {
        v->pfreq = v->freq;
        v->pvol = v->vol;
        v->note = n;
        if (nt.custom && !(nt.fx == 1 && v->inst == nt.wave)) {
            v->inst = nt.wave;
            v->it = 0;
            v->inote = -1;
        } else if (!nt.custom) {
            v->inst = -1;
        }
    }
    float out;
    if (nt.custom && v->inst >= 0) {
        /* the instrument: its own notes, transposed, at its own speed */
        double ifrac;
        int ls, le, end;
        bounds(v->inst, &ls, &le, &end);
        int in = note_index(v->it, v->inst, 0, end, 0, &ifrac);
        if (in < 0) {
            out = 0;
        } else {
            note_t inn = get_note(v->inst, in);
            if (in != v->inote) {
                v->ipfreq = v->ifreq;
                v->ipvol = v->ivol;
                v->inote = in;
            }
            inn.custom = 0;
            float fr, vo;
            out = play_note(v, v->inst, inn, in, ifrac, speed_of(v->inst), &v->iphase, &fr, &vo, v->ipfreq, v->ipvol,
                            (float)nt.pitch - 24.0f);
            v->ifreq = fr;
            v->ivol = vo;
            /* the note's own volume and effect scale the instrument */
            float a = nt.vol / 7.0f;
            if (nt.fx == 4) a *= (float)frac;
            if (nt.fx == 5) a *= 1.0f - (float)frac;
            out *= a;
            v->freq = pitch_hz((float)nt.pitch);
            v->vol = nt.vol / 7.0f;
        }
        v->it += 1;
    } else {
        out = play_note(v, v->sfx, nt, n, frac, speed, &v->phase, &v->freq, &v->vol, v->pfreq, v->pvol, 0);
    }
    v->phase2 += 0.6f / (float)rate;
    if (v->phase2 >= 1) v->phase2 -= 1;
    /* filters of the sound effect (byte 64): buzz, dampen */
    uint8_t flt = ram[SFX_BASE + v->sfx * 68 + 64];
    if (flt & 4)
        out = out * 0.7f + (v->phase < 0.5f ? 0.05f : -0.05f) * v->vol;
    int damp = flt / 72 % 3;
    if (damp) {
        float k = damp == 1 ? 0.35f : 0.18f;
        v->lp += (out - v->lp) * k;
        out = v->lp;
    }
    v->t += 1;
    return out;
}

/* ------------------------------------------------------------ music */

static int pattern_empty(int p)
{
    const uint8_t *m = ram + MUSIC_BASE + p * 4;
    return (m[0] & 0x40) && (m[1] & 0x40) && (m[2] & 0x40) && (m[3] & 0x40);
}

static void pattern_start(int p)
{
    if (p < 0 || p > 63 || pattern_empty(p)) {
        mpat = -1;
        for (int c = 0; c < CHANS; c++)
            voice_stop(&ch[c].music);
        return;
    }
    const uint8_t *m = ram + MUSIC_BASE + p * 4;
    mpat = p;
    mt = 0;
    mlen = 0;
    double loop_len = 0;
    for (int c = 0; c < CHANS; c++) {
        if (m[c] & 0x40) {
            voice_stop(&ch[c].music);
            continue;
        }
        int s = m[c] & 63, ls, le, end;
        voice_start(&ch[c].music, s, 0, 0);
        bounds(s, &ls, &le, &end);
        double len = speed_of(s) * (double)unit * (le > ls ? 32 : end);
        if (le > ls) {
            if (!loop_len) loop_len = len;
        } else if (!mlen) {
            mlen = len;                     /* the leftmost sound that does not loop */
        }
    }
    if (!mlen)
        mlen = loop_len ? loop_len : 32 * unit;
    mcount++;
}

static void music_next(void)
{
    const uint8_t *m = ram + MUSIC_BASE + mpat * 4;
    if (m[2] & 0x80) {
        pattern_start(-1);
        return;
    }
    if (m[1] & 0x80) {
        int p = mpat;
        while (p > 0 && !(ram[MUSIC_BASE + p * 4] & 0x80))
            p--;
        pattern_start(p);
        return;
    }
    pattern_start(mpat + 1);
}

/* ------------------------------------------------------------ requests */

static void do_sfx(int n, int c, int off, int len)
{
    if (n == -1) {
        for (int i = 0; i < CHANS; i++)
            if (c < 0 || c == i)
                voice_stop(&ch[i].game);
        return;
    }
    if (n == -2) {
        for (int i = 0; i < CHANS; i++)
            if ((c < 0 || c == i) && ch[i].game.sfx >= 0) {
                voice_t *v = &ch[i].game;
                double frac;
                int cur = note_index(v->t, v->sfx, v->first, v->end, 0, &frac);
                double len = speed_of(v->sfx) * (double)unit;
                v->released = 1;
                v->t = cur >= 0 ? (cur - v->first + frac) * len : v->t;
                v->end = 32;
            }
        return;
    }
    if (n < 0 || n > 63)
        return;
    if (c < 0 || c >= CHANS) {
        /* the same sound again on its channel, else a free one, else one
         * the music does not use, else the last */
        c = -1;
        for (int i = 0; i < CHANS && c < 0; i++)
            if (ch[i].game.sfx == n)
                c = i;
        for (int i = 0; i < CHANS && c < 0; i++)
            if (ch[i].game.sfx < 0 && ch[i].music.sfx < 0)
                c = i;
        for (int i = 0; i < CHANS && c < 0; i++)
            if (ch[i].game.sfx < 0 && !(mmask >> i & 1))
                c = i;
        for (int i = 0; i < CHANS && c < 0; i++)
            if (ch[i].game.sfx < 0)
                c = i;
        if (c < 0)
            c = CHANS - 1;
    }
    voice_start(&ch[c].game, n, off, len);
}

static void do_music(int n, int fade, int mask)
{
    if (n < 0) {
        if (fade > 0 && mpat >= 0) {
            mfade_step = -1.0f / ((float)fade * rate / 1000.0f);
            mstop_after_fade = 1;
        } else {
            pattern_start(-1);
        }
        return;
    }
    mmask = mask & 15;
    mcount = 0;
    mstop_after_fade = 0;
    if (fade > 0) {
        mfade = 0;
        mfade_step = 1.0f / ((float)fade * rate / 1000.0f);
    } else {
        mfade = 1;
        mfade_step = 0;
    }
    pattern_start(n);
}

static void reset_all(void)
{
    memset(ch, 0, sizeof ch);
    for (int c = 0; c < CHANS; c++) {
        voice_stop(&ch[c].game);
        voice_stop(&ch[c].music);
    }
    mpat = -1;
    mfade = 1;
    mfade_step = 0;
    paused = 0;
}

static void take_requests(void)
{
    while (qtail != qhead) {
        unsigned t = qtail;
        cmd_t c = { q[t].type, q[t].a, q[t].b, q[t].c, q[t].d, q[t].ram };
        __asm__ volatile("" ::: "memory");
        qtail = (t + 1) % QLEN;
        switch (c.type) {
        case C_ATTACH:
            unit = 183.0f * (float)rate / 22050.0f;
            reset_all();
            ram = c.ram;
            break;
        case C_SFX:
            if (ram) do_sfx(c.a, c.b, c.c, c.d);
            break;
        case C_MUSIC:
            if (ram) do_music(c.a, c.b, c.c);
            break;
        case C_PAUSE:
            paused = c.a;
            break;
        }
    }
}

void n8snd_mix(int16_t *out, unsigned n, float gain)
{
    take_requests();
    if (!ram || paused)
        return;
    for (unsigned i = 0; i < n; i++) {
        float s = 0;
        for (int c = 0; c < CHANS; c++) {
            float m = voice_sample(&ch[c].music) * mfade;
            float g = voice_sample(&ch[c].game);
            s += ch[c].game.sfx >= 0 ? g : m;
        }
        if (mpat >= 0) {
            mt += 1;
            if (mt >= mlen)
                music_next();
            if (mfade_step) {
                mfade += mfade_step;
                if (mfade >= 1) { mfade = 1; mfade_step = 0; }
                if (mfade <= 0) {
                    mfade = 0;
                    mfade_step = 0;
                    if (mstop_after_fade) pattern_start(-1);
                }
            }
        }
        int add = (int)(s * gain * 0.55f * 32767.0f);
        for (int c = 0; c < 2; c++) {
            int v = out[2 * i + c] + add;
            out[2 * i + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

int n8snd_stat(int n)
{
    if (!ram)
        return n == 57 ? 0 : -1;
    int c = (n >= 46 && n <= 49) ? n - 46 : (n >= 16 && n <= 19) ? n - 16 : -1;
    if (c >= 0)
        return ch[c].game.sfx >= 0 ? ch[c].game.sfx : ch[c].music.sfx;
    c = (n >= 50 && n <= 53) ? n - 50 : (n >= 20 && n <= 23) ? n - 20 : -1;
    if (c >= 0) {
        voice_t *v = ch[c].game.sfx >= 0 ? &ch[c].game : &ch[c].music;
        return v->sfx >= 0 ? v->note : -1;
    }
    switch (n) {
    case 24: case 54: return mpat;
    case 25: case 55: return mcount;
    case 26: case 56: return mpat >= 0 ? (int)(mt / unit) : 0;
    case 57: return mpat >= 0;
    }
    return 0;
}
