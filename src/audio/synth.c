#include "synth.h"

#include <math.h>
#include <string.h>

#define MAX_ENVELOPE_SECONDS 2.0f
#define KNEE 0.85f
#define TWO_PI 6.2831853f
#define INV_2PI 0.15915494f
#define TO_UNIT (1.0f / 4294967296.0f)

/* the clean voice: no envelope stage shorter than this (no clicks) */
#define MIN_ATTACK_S    0.0007f
#define MIN_RELEASE_S   0.003f
#define ENV_DONE        0.255f          /* 0..255: below it (-60 dB) the voice is idle */
#define ENV_K           3.0f            /* decays: 5% (-26 dB) left at the time asked, -60 dB at 2.3 times */

/* the room's delay lines (primes, at the largest size) and its diffusers */
static const uint16_t room_base[SYNTH_ROOM_LINES] = { 1559, 1801, 2039, 2297, 2503, 2753, 3011, 3299 };
static const uint16_t room_ap_len[2] = { 71, 190 };     /* at half the rate */
#define ROOM_AP_G   0.6f
#define ROOM_OUT    0.30f               /* calibrated: a full send is about as loud as the dry sound */
#define QUIET       1e-6f
#define COMP_T      0.6f                /* the compressor's threshold (-4.4 dB) ... */
#define COMP_RELEASE 0.012f             /* ... and how fast it lets go: 110 ms in blocks of 64 */
#define COMP_MAKEUP 1.25f               /* +2 dB back: the loud parts end near 0.75 ... */
#define COMP_TOP    (0.8f / COMP_MAKEUP)  /* ... and never past 0.8 */

void synth_room(synth_t *s, float size, float damp, float wet)
{
    size = size < 0 ? 0 : size > 1 ? 1 : size;
    damp = damp < 0 ? 0 : damp > 1 ? 1 : damp;
    s->room_size = size;
    s->room_damp = damp;
    s->room_wet = wet < 0 ? 0 : wet > 1 ? 1 : wet;
    float rt60 = 0.25f + 3.75f * size * size;             /* seconds */
    for (int i = 0; i < SYNTH_ROOM_LINES; i++) {
        /* the room runs at half the rate: half the samples for the same time */
        uint16_t len = (uint16_t)(room_base[i] * (0.45f + 0.55f * size) * (float)s->rate / 96000.0f);
        if (len < 64)
            len = 64;
        if (len > SYNTH_ROOM_LEN)
            len = SYNTH_ROOM_LEN;
        s->room_len[i] = len;
        if (s->room_i[i] >= len)
            s->room_i[i] = 0;
        s->room_g[i] = powf(10.0f, -3.0f * (float)len / (rt60 * (float)s->rate * 0.5f));
    }
}

void synth_echo(synth_t *s, float ms, float feedback, float wet)
{
    float n = ms * 0.001f * (float)s->rate;
    if (n < 1)
        n = 1;
    if (n > SYNTH_ECHO_LEN - 1)
        n = SYNTH_ECHO_LEN - 1;
    s->echo_len = (uint32_t)n;
    s->echo_fb = feedback < 0 ? 0 : feedback > 0.95f ? 0.95f : feedback;
    s->echo_wet = wet < 0 ? 0 : wet > 1 ? 1 : wet;
}

void synth_init(synth_t *s, uint32_t rate)
{
    memset(s, 0, sizeof *s);
    s->rate = rate;
    s->gain = 1.0f;
    s->rng = 0x9E3779B9u;
    s->comp_gain = COMP_MAKEUP;
    for (unsigned i = 0; i < SYNTH_VOICES; i++) {
        synth_voice_t *v = &s->v[i];
        v->stage = SYNTH_IDLE;
        v->lfsr = 1;
        v->noise = v->noise0 = -1.0f;
        v->env_r[0] = v->env_r[1] = v->env_r[2] = v->env_r[3] = 0xFF;
        v->env_c[0] = v->env_c[1] = v->env_c[2] = v->env_c[3] = -1.0f;
    }
    synth_room(s, 0.45f, 0.45f, 1.0f);
    synth_echo(s, 330.0f, 0.35f, 1.0f);
}

float synth_rate_increment(uint8_t rate, uint32_t sample_rate)
{
    if (rate == 0)
        return 256.0f;
    float samples = rate / 255.0f * MAX_ENVELOPE_SECONDS * (float)sample_rate;
    if (samples < 1.0f)
        samples = 1.0f;
    return 255.0f / samples;
}

void synth_retrigger(synth_t *s, unsigned ch)
{
    if (ch < SYNTH_VOICES)
        s->v[ch].gated = 0;
}

unsigned synth_active(const synth_t *s)
{
    unsigned n = 0;
    for (unsigned i = 0; i < SYNTH_VOICES; i++)
        n += s->v[i].stage != SYNTH_IDLE;
    return n;
}

float synth_limit(float x)
{
    float a = x < 0 ? -x : x;
    if (a <= KNEE)
        return x;
    float t = (a - KNEE) * (1.0f / (1.0f - KNEE));
    a = KNEE + (1.0f - KNEE) * t / (1.0f + t);
    return x < 0 ? -a : a;
}

float synth_cutoff_hz(uint8_t reg)
{
    /* 20 Hz .. 20 kHz in 254 steps of about half a semitone */
    return 20.0f * exp2f((float)(reg ? reg - 1 : 0) * (9.9658f / 254.0f));
}

/* sin(2 pi p) for p in 0..1: sin(2 pi p) = -sin(pi q) with q = 2p - 1,
 * and sin(pi q) a parabola refined once (error about 0.1%) */
static inline float sine(float p)
{
    float q = p * 2.0f - 1.0f;
    float y = 4.0f * q * (1.0f - (q < 0 ? -q : q));
    y = 0.225f * (y * (y < 0 ? -y : y) - y) + y;
    return -y;
}

static inline float frac(float x)
{
    return x - floorf(x);
}

/* the band-limited step and corner (polynomial, two samples): what the
 * naive wave lacks around a jump (BLEP) or a change of slope (BLAMP) */
static inline float blep(float t, float dt, float idt)
{
    if (t < dt) {
        t *= idt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float blamp(float t, float dt, float idt)
{
    if (t < dt) {
        t = 1.0f - t * idt;
        return t * t * t * (1.0f / 6.0f);
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt + 1.0f;
        return t * t * t * (1.0f / 6.0f);
    }
    return 0.0f;
}

static inline uint32_t xorshift(uint32_t *r)
{
    uint32_t x = *r;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}

static inline float white(synth_t *s)
{
    return (float)(int32_t)xorshift(&s->rng) * (1.0f / 2147483648.0f);
}

/* soft saturation (a rational tanh), flat at +-1 past +-3 */
static inline float soft(float x)
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* the coefficient that moves an exponential this close (k: ln of 1/left)
 * to its target in `seconds` */
static float ecoef(float seconds, float k, uint32_t rate)
{
    return expf(-k / (seconds * (float)rate));
}

static float reg_seconds(uint8_t r)
{
    return r / 255.0f * MAX_ENVELOPE_SECONDS;
}

/* the clean envelope's coefficients for the voice's ADSR and filter decay */
static void env_coefs(synth_t *s, synth_voice_t *v, const volatile uint8_t *r)
{
    uint8_t a = r[SYNTH_ATTACK], d = r[SYNTH_DECAY], rl = r[SYNTH_RELEASE], fd = r[SYNTH_FDECAY];
    if (a != v->env_r[0]) {
        float t = reg_seconds(a);
        if (t < MIN_ATTACK_S)
            t = MIN_ATTACK_S;
        /* towards 1.3 so that 1.0 is reached at t: the curve of an RC */
        v->env_c[0] = 1.0f - ecoef(t, 1.466f, s->rate);
        v->env_r[0] = a;
    }
    if (d != v->env_r[1]) {
        float t = reg_seconds(d);
        v->env_c[1] = t < MIN_ATTACK_S ? 0.0f : ecoef(t, ENV_K, s->rate);  /* 5% (-26 dB) left at t */
        v->env_r[1] = d;
    }
    if (rl != v->env_r[2]) {
        float t = reg_seconds(rl);
        if (t < MIN_RELEASE_S)
            t = MIN_RELEASE_S;
        v->env_c[2] = ecoef(t, ENV_K, s->rate);
        v->env_r[2] = rl;
    }
    if (fd != v->env_r[3]) {
        float t = reg_seconds(fd);
        v->env_c[3] = t < MIN_ATTACK_S ? 1.0f : ecoef(t, ENV_K, s->rate); /* 0: it stays open */
        v->env_r[3] = fd;
    }
}

/* the plucked string: the delay line filled with the shape of the string
 * pulled aside by the pick (a fundamental-rich triangle) and a burst of
 * noise, as bright as asked */
static void pluck_excite(synth_t *s, synth_voice_t *v, unsigned ch, uint32_t inc, uint8_t bright)
{
    float *buf = s->pluck[ch];
    float b = bright / 255.0f;
    float a = 0.1f + 0.9f * b;          /* the burst's own low pass */
    float lp = 0, mean = 0;
    float d = inc ? 4294967296.0f / (float)inc : SYNTH_PLUCK_LEN;
    unsigned n = d + 3 > SYNTH_PLUCK_LEN ? SYNTH_PLUCK_LEN : (unsigned)(d + 3);
    float pick = 0.18f * (float)n;
    for (unsigned i = 0; i < SYNTH_PLUCK_LEN; i++)
        buf[i] = 0;
    for (unsigned i = 0; i < n; i++) {
        float shape = (float)i < pick ? (float)i / pick : 1.0f - ((float)i - pick) / ((float)n - pick);
        lp += a * (white(s) - lp);
        buf[i] = shape + (0.25f + 0.75f * b) * lp;
        mean += buf[i];
    }
    mean /= (float)n;
    float peak = 0;
    for (unsigned i = 0; i < n; i++) {
        buf[i] -= mean;
        float m = buf[i] < 0 ? -buf[i] : buf[i];
        if (m > peak)
            peak = m;
    }
    if (peak > 0)
        for (unsigned i = 0; i < n; i++)
            buf[i] *= 1.0f / peak;
    v->ks_w = (uint16_t)n;
    v->ks_lp = v->ks_ax = v->ks_ay = 0;
}

/* ---------------------------------------------------------------- waves */

/* The raw voice: the chip of the first versions, sample for sample. */
static void wave_raw(synth_voice_t *v, uint8_t wave, uint32_t duty, uint32_t inc, int fast_noise,
                     float *out, unsigned n)
{
    uint32_t phase = v->phase;
    int noise = wave == SYNTH_NOISE || wave == SYNTH_METAL;
    int tap = wave == SYNTH_METAL ? 6 : 1;      /* feedback bit: 93-step or 32767-step sequence */
    for (unsigned i = 0; i < n; i++) {
        float value;
        uint32_t prev = phase;
        phase += inc;
        if (noise) {
            if (fast_noise || phase < prev) {
                unsigned b0 = v->lfsr & 1, b1 = v->lfsr >> tap & 1;
                v->lfsr = (uint16_t)(v->lfsr >> 1 | (b0 ^ b1) << 14);
                v->noise = b0 ? 1.0f : -1.0f;
            }
            value = v->noise;
        } else if (wave == SYNTH_SQUARE) {
            value = phase < duty ? 1.0f : -1.0f;
        } else {
            float p = (float)phase * TO_UNIT;
            if (wave == SYNTH_TRIANGLE)
                value = p < 0.5f ? -1.0f + 4.0f * p : 3.0f - 4.0f * p;
            else if (wave == SYNTH_SINE)
                value = sine(p);
            else
                value = 2.0f * p - 1.0f;
        }
        out[i] = value;
    }
    v->phase = phase;
}

typedef struct {
    uint32_t inc;           /* phase step at the start of the block ... */
    int32_t dinc;           /* ... and its change per sample */
    float pw;               /* square: the high part, 0..1 */
    uint8_t mod1, mod2;
    uint8_t fast;           /* noise: a step every sample */
    float depth0, depth1;   /* FM depth at the start and end of the block (rad) */
    float fb;               /* FM feedback (rad) */
} wave_args_t;

/* The clean voice. */
static void wave_clean(synth_t *s, synth_voice_t *v, unsigned ch, uint8_t wave, const wave_args_t *a,
                       float *out, unsigned n)
{
    uint32_t phase = v->phase, inc = a->inc;
    int32_t dinc = a->dinc;
    float dt = (float)(inc + dinc * (int32_t)(n / 2)) * TO_UNIT;
    if (dt < 1e-7f)
        dt = 1e-7f;
    float idt = 1.0f / dt;

    switch (wave) {
    case SYNTH_SQUARE: {
        float pw = a->pw;
        if (dt >= 0.5f || pw <= 0.0f) {    /* above the Nyquist frequency, or duty 0: nothing to hear */               /* above the Nyquist frequency: nothing to hear */
            for (unsigned i = 0; i < n; i++)
                out[i] = 0;
            break;
        }
        if (pw < dt)
            pw = dt;
        if (pw > 1.0f - dt)
            pw = 1.0f - dt;
        for (unsigned i = 0; i < n; i++) {
            float t = (float)phase * TO_UNIT;
            float y = t < pw ? 1.0f : -1.0f;
            y += blep(t, dt, idt);
            float t2 = t - pw;
            if (t2 < 0)
                t2 += 1.0f;
            y -= blep(t2, dt, idt);
            out[i] = y;
            phase += inc;
            inc += dinc;
        }
        break;
    }
    case SYNTH_SAW:
        for (unsigned i = 0; i < n; i++) {
            float t = (float)phase * TO_UNIT;
            out[i] = dt < 0.5f ? 2.0f * t - 1.0f - blep(t, dt, idt) : 0.0f;
            phase += inc;
            inc += dinc;
        }
        break;
    case SYNTH_TRIANGLE:
        for (unsigned i = 0; i < n; i++) {
            float t = (float)phase * TO_UNIT;
            float y = t < 0.5f ? -1.0f + 4.0f * t : 3.0f - 4.0f * t;
            if (dt < 0.5f) {
                float t2 = t + 0.5f;
                if (t2 >= 1.0f)
                    t2 -= 1.0f;
                y += 8.0f * dt * (blamp(t, dt, idt) - blamp(t2, dt, idt));
            } else {
                y = 0;
            }
            out[i] = y;
            phase += inc;
            inc += dinc;
        }
        break;
    case SYNTH_SINE:
        for (unsigned i = 0; i < n; i++) {
            out[i] = sine((float)phase * TO_UNIT);
            phase += inc;
            inc += dinc;
        }
        break;
    case SYNTH_NOISE:
    case SYNTH_METAL: {
        /* the LFSR's steps joined by straight lines: the same noise as the
         * chip's, without its hiss of steps */
        int tap = wave == SYNTH_METAL ? 6 : 1;
        int fast = a->fast;
        for (unsigned i = 0; i < n; i++) {
            uint32_t prev = phase;
            phase += inc;
            inc += dinc;
            if (fast || phase < prev) {
                unsigned b0 = v->lfsr & 1, b1 = v->lfsr >> tap & 1;
                v->lfsr = (uint16_t)(v->lfsr >> 1 | (b0 ^ b1) << 14);
                v->noise0 = v->noise;
                v->noise = b0 ? 1.0f : -1.0f;
            }
            out[i] = fast ? v->noise : v->noise0 + (v->noise - v->noise0) * ((float)phase * TO_UNIT);
        }
        break;
    }
    case SYNTH_FM: {
        uint32_t ratio = a->mod1 ? a->mod1 : 16;      /* x/16 */
        uint32_t p2 = v->phase2;
        float depth = a->depth0, ddepth = (a->depth1 - a->depth0) / (float)n;
        float fb = a->fb * INV_2PI, m = v->fm_fb;
        for (unsigned i = 0; i < n; i++) {
            m = sine(frac((float)p2 * TO_UNIT + fb * m));
            out[i] = sine(frac((float)phase * TO_UNIT + depth * INV_2PI * m));
            p2 += (uint32_t)(((uint64_t)inc * ratio) >> 4);
            phase += inc;
            inc += dinc;
            depth += ddepth;
        }
        v->phase2 = p2;
        v->fm_fb = m;
        break;
    }
    case SYNTH_PLUCK: {
        float *buf = s->pluck[ch];
        float bright = (a->mod1 ? a->mod1 : 140) / 255.0f;
        float sus = (a->mod2 ? a->mod2 : 160) / 255.0f;
        /* the loop's damping: (1 - st) x[n] + st x[n-1], half a sample of
         * delay and dull at st = 0.5 (Karplus-Strong's own average), nearly
         * nothing at 0.02; lighter on the high notes, which go round the
         * loop more often */
        float hz = dt * (float)s->rate;
        float st = (0.5f - 0.47f * bright) * (hz > 220.0f ? 220.0f / hz : 1.0f);
        if (st < 0.02f)
            st = 0.02f;
        float g = 1.0f - 0.012f * (1.0f - sus) * (1.0f - sus) - 0.00002f;
        unsigned w = v->ks_w;
        float lp = v->ks_lp, ax = v->ks_ax, ay = v->ks_ay;
        for (unsigned i = 0; i < n; i++) {
            /* the delay: whole samples, then a first-order allpass for the
             * fraction (Thiran: no loss of the highs, unlike a straight line) */
            float d = (inc ? 4294967296.0f / (float)inc : 2.0f) - st;
            if (d < 2.0f)
                d = 2.0f;
            if (d > SYNTH_PLUCK_LEN - 3)
                d = SYNTH_PLUCK_LEN - 3;
            unsigned di = (unsigned)(d - 0.1f);
            float f = d - (float)di;            /* 0.1 .. 1.1 */
            float c = (1.0f - f) / (1.0f + f);
            float x = buf[(w - di) & (SYNTH_PLUCK_LEN - 1)];
            float y = c * x + ax - c * ay;
            ax = x;
            ay = y;
            float o = ((1.0f - st) * y + st * lp) * g;
            lp = y;
            buf[w] = o;
            w = (w + 1) & (SYNTH_PLUCK_LEN - 1);
            out[i] = o;
            phase += inc;
            inc += dinc;
        }
        v->ks_w = (uint16_t)w;
        v->ks_lp = lp;
        v->ks_ax = ax;
        v->ks_ay = ay;
        break;
    }
    case SYNTH_SUPERSAW: {
        uint32_t spread = (a->mod1 ? a->mod1 : 100) * 6u;       /* /65536: up to 2.3%, 40 cents */
        uint32_t p2 = v->phase2, p3 = v->phase3;
        for (unsigned i = 0; i < n; i++) {
            uint32_t ds = (uint32_t)(((uint64_t)inc * spread) >> 16);
            uint32_t i2 = inc - ds, i3 = inc + ds;
            float d2 = (float)i2 * TO_UNIT, d3 = (float)i3 * TO_UNIT;
            float t1 = (float)phase * TO_UNIT, t2 = (float)p2 * TO_UNIT, t3 = (float)p3 * TO_UNIT;
            float y = 0;
            if (dt < 0.5f)
                y += 2.0f * t1 - 1.0f - blep(t1, dt, idt);
            if (d2 < 0.5f)
                y += 0.75f * (2.0f * t2 - 1.0f - blep(t2, d2, 1.0f / d2));
            if (d3 < 0.5f)
                y += 0.75f * (2.0f * t3 - 1.0f - blep(t3, d3, 1.0f / d3));
            out[i] = y * 0.55f;
            phase += inc;
            p2 += i2;
            p3 += i3;
            inc += dinc;
        }
        v->phase2 = p2;
        v->phase3 = p3;
        break;
    }
    case SYNTH_ORGAN: {
        float bar[4];
        if (!a->mod1 && !a->mod2) {
            bar[0] = 15; bar[1] = 9; bar[2] = 5; bar[3] = 3;
        } else {
            bar[0] = (float)(a->mod1 >> 4); bar[1] = (float)(a->mod1 & 15);
            bar[2] = (float)(a->mod2 >> 4); bar[3] = (float)(a->mod2 & 15);
        }
        float sum = bar[0] + bar[1] + bar[2] + bar[3];
        float k = sum > 0 ? 1.0f / sum * 1.2f : 0;
        for (int h = 0; h < 4; h++) {
            bar[h] *= k;
            if (dt * (float)(h + 1) >= 0.5f)
                bar[h] = 0;                     /* a harmonic past Nyquist is left out */
        }
        for (unsigned i = 0; i < n; i++) {
            out[i] = bar[0] * sine((float)phase * TO_UNIT) + bar[1] * sine((float)(phase * 2u) * TO_UNIT) +
                     bar[2] * sine((float)(phase * 3u) * TO_UNIT) + bar[3] * sine((float)(phase * 4u) * TO_UNIT);
            phase += inc;
            inc += dinc;
        }
        break;
    }
    default:
        for (unsigned i = 0; i < n; i++)
            out[i] = 0;
        break;
    }
    v->phase = phase;
}

/* ---------------------------------------------------------------- voices */

static float lfo_hz(uint8_t r)
{
    return 0.1f * exp2f((float)(r - 1) * (7.64f / 254.0f));    /* 0.1 .. 20 Hz */
}

/* One voice added into the mix and the sends (n <= SYNTH_BLOCK). The
 * registers are read once: everything derived from them is computed once
 * per block, the volume and the pitch ramp across it. */
static void render_voice(synth_t *s, unsigned ch, const volatile uint8_t *r, unsigned n)
{
    synth_voice_t *v = &s->v[ch];
    uint8_t ctl = r[SYNTH_CONTROL];
    int gate = ctl & SYNTH_GATE;
    int raw = (ctl & SYNTH_RAW) || (r[SYNTH_FLAGS] & SYNTH_FLAG_RAW) || s->retro;
    uint8_t wave = r[SYNTH_WAVEFORM];
    if (wave >= SYNTH_WAVES)
        wave = SYNTH_SQUARE;
    uint32_t f88 = (uint32_t)r[SYNTH_FREQ_LO] << 8 | (uint32_t)r[SYNTH_FREQ_HI] << 16 | r[SYNTH_FREQ_FRAC];
    if (f88 != v->f88) {                /* a 64-bit division: only when the pitch moves */
        v->f88 = f88;
        v->f88_inc = (uint32_t)(((uint64_t)f88 << 24) / s->rate);
    }
    uint32_t inc = v->f88_inc;

    if (gate && !v->gated) {
        if (v->stage == SYNTH_IDLE || v->level <= 0.0f) {
            v->level = 0;
            v->phase = 0;               /* a silent voice starts the wave from 0 */
            v->phase2 = 0;
            v->f1 = v->f2 = 0;
            v->fm_fb = 0;
            v->fresh = 1;
            if (wave == SYNTH_SUPERSAW && !raw) {
                v->phase2 = xorshift(&s->rng);  /* the other saws anywhere: no flanging start */
                v->phase3 = xorshift(&s->rng);
            }
        }
        if (v->stage == SYNTH_IDLE)
            v->fenv = 0;
        v->stage = SYNTH_ATTACK_ST;     /* a sounding one attacks from its level */
        v->menv = 1.0f;
        if (wave == SYNTH_PLUCK && !raw)
            pluck_excite(s, v, ch, inc, r[SYNTH_MOD1] ? r[SYNTH_MOD1] : 140);
    } else if (!gate && v->gated && v->stage != SYNTH_IDLE) {
        v->stage = SYNTH_RELEASE_ST;
    }
    v->gated = (uint8_t)gate;
    if (v->stage == SYNTH_IDLE)
        return;                 /* silent: no oscillator work at all */

    float *buf = s->vbuf;
    float vol = r[SYNTH_VOLUME] / 255.0f * (1.0f / 255.0f);    /* level is 0..255 */

    if (raw) {
        /* -------- the chip: naive wave, straight envelope */
        uint32_t duty = r[SYNTH_DUTY] * 0x01010101u;
        int fast_noise = (f88 >> 8) >= s->rate;     /* at least one step per sample */
        wave_raw(v, wave > SYNTH_METAL ? SYNTH_SQUARE : wave, duty, inc, fast_noise, buf, n);
        float atk = synth_rate_increment(r[SYNTH_ATTACK], s->rate);
        float dec = synth_rate_increment(r[SYNTH_DECAY], s->rate);
        float sus = r[SYNTH_SUSTAIN];
        float rel = synth_rate_increment(r[SYNTH_RELEASE], s->rate);
        float level = v->level;
        uint8_t stage = v->stage;
        unsigned i;
        for (i = 0; i < n; i++) {
            switch (stage) {
            case SYNTH_ATTACK_ST:
                level += atk;
                if (level >= 255.0f) { level = 255.0f; stage = SYNTH_DECAY_ST; }
                break;
            case SYNTH_DECAY_ST:
                level -= dec;
                if (level <= sus) { level = sus; stage = SYNTH_SUSTAIN_ST; }
                break;
            case SYNTH_SUSTAIN_ST:
                level = sus;
                break;
            case SYNTH_RELEASE_ST:
                level -= rel;
                if (level <= 0.0f) { level = 0.0f; stage = SYNTH_IDLE; }
                break;
            }
            buf[i] *= level * vol;
            if (stage == SYNTH_IDLE) {
                i++;
                break;
            }
        }
        for (; i < n; i++)
            buf[i] = 0;
        v->level = level;
        v->stage = stage;
        v->inc = inc;
        v->vol = vol;
        v->fresh = 0;
    } else {
        /* -------- the clean voice */
        env_coefs(s, v, r);
        uint32_t inc0 = v->fresh ? inc : v->inc;
        float vol0 = v->fresh ? vol : v->vol;
        wave_args_t a;
        a.inc = inc0;
        a.dinc = n == SYNTH_BLOCK ? (int32_t)(inc - inc0) / SYNTH_BLOCK : (int32_t)(inc - inc0) / (int32_t)n;
        a.mod1 = r[SYNTH_MOD1];
        a.mod2 = r[SYNTH_MOD2];
        a.fast = (f88 >> 8) >= s->rate;

        /* the LFO, once a block */
        float lfo = 0;
        if (r[SYNTH_LFO_RATE]) {
            lfo = sine((float)v->lfo * TO_UNIT);
            v->lfo += (uint32_t)(lfo_hz(r[SYNTH_LFO_RATE]) * (float)n / (float)s->rate * 4294967296.0f);
        }
        a.pw = r[SYNTH_DUTY] / 256.0f + lfo * r[SYNTH_LFO_PWM] * (0.45f / 255.0f);
        if (a.pw < 0.02f) a.pw = r[SYNTH_DUTY] ? 0.02f : 0.0f;
        if (a.pw > 0.98f) a.pw = 0.98f;

        /* FM depth: fades with MODDECAY */
        float depth = (a.mod2 ? a.mod2 : 40) * (1.0f / 32.0f);
        a.depth0 = depth * v->menv;
        if (r[SYNTH_MODDECAY]) {
            float c = ecoef(reg_seconds(r[SYNTH_MODDECAY]) + 0.001f, 4.6f, s->rate);
            v->menv *= powf(c, (float)n);
        }
        a.depth1 = depth * v->menv;
        a.fb = r[SYNTH_FMFB] * (1.6f / 255.0f);

        wave_clean(s, v, ch, wave, &a, buf, n);

        /* noise, drive, filter */
        uint8_t nm = r[SYNTH_NOISEMIX];
        if (nm) {
            float k = nm / 255.0f;
            for (unsigned i = 0; i < n; i++)
                buf[i] += k * white(s);
        }
        uint8_t drive = r[SYNTH_DRIVE];
        if (drive) {
            float pre = 1.0f + drive * (12.0f / 255.0f), post = 1.0f / soft(pre);
            for (unsigned i = 0; i < n; i++)
                buf[i] = soft(buf[i] * pre) * post;
        }
        uint8_t cut = r[SYNTH_CUTOFF];
        /* the filter envelope: up with the attack, down with its decay */
        if (v->stage == SYNTH_ATTACK_ST)
            v->fenv += (1.0f - v->fenv) * (1.0f - powf(1.0f - v->env_c[0], (float)n));
        else
            v->fenv *= powf(v->env_c[3], (float)n);
        if (cut) {
            float hz = synth_cutoff_hz(cut);
            uint8_t fm = r[SYNTH_FILTER];
            if (fm & SYNTH_KEYTRACK)
                hz *= (float)f88 * (1.0f / 256.0f) * (1.0f / 261.63f);
            float oct = (int8_t)r[SYNTH_FENV] * (1.0f / 16.0f) * v->fenv + lfo * r[SYNTH_LFO_CUT] * (4.0f / 255.0f);
            if (oct != 0.0f)
                hz *= exp2f(oct);
            float top = 0.45f * (float)s->rate;
            if (hz > top)
                hz = top;
            if (hz < 10.0f)
                hz = 10.0f;
            /* the state variable filter of Zavalishin and Simper (TPT) */
            float g = tanf(3.14159265f * hz / (float)s->rate);
            float k = 2.0f - 1.94f * (r[SYNTH_RESONANCE] / 255.0f);
            float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            float ic1 = v->f1, ic2 = v->f2;
            int mode = fm & 3;
            for (unsigned i = 0; i < n; i++) {
                float v0 = buf[i];
                float v3 = v0 - ic2;
                float v1 = a1 * ic1 + a2 * v3;
                float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                float y;
                switch (mode) {
                case SYNTH_LOWPASS: y = v2; break;
                case SYNTH_BANDPASS: y = v1 * k; break;     /* 0 dB at the peak */
                case SYNTH_HIGHPASS: y = v0 - k * v1 - v2; break;
                default: y = v0 - k * v1; break;            /* notch */
                }
                buf[i] = y;
            }
            /* a denormal is slow on some FPUs: the state flushed to 0 */
            v->f1 = (ic1 > 1e-15f || ic1 < -1e-15f) ? ic1 : 0;
            v->f2 = (ic2 > 1e-15f || ic2 < -1e-15f) ? ic2 : 0;
        }

        /* the envelope and the volume */
        float ca = v->env_c[0], cd = v->env_c[1], cr = v->env_c[2];
        float sus = r[SYNTH_SUSTAIN];
        float level = v->level;
        uint8_t stage = v->stage;
        float dvol = (vol - vol0) / (float)n, vv = vol0;
        unsigned i;
        for (i = 0; i < n; i++) {
            switch (stage) {
            case SYNTH_ATTACK_ST:
                level += (331.5f - level) * ca;
                if (level >= 255.0f) { level = 255.0f; stage = SYNTH_DECAY_ST; }
                break;
            case SYNTH_DECAY_ST:
                level = sus + (level - sus) * cd;
                if (level - sus < 0.0255f) { level = sus; stage = SYNTH_SUSTAIN_ST; }
                break;
            case SYNTH_SUSTAIN_ST:
                level += (sus - level) * 0.01f;
                break;
            case SYNTH_RELEASE_ST:
                level *= cr;
                if (level < ENV_DONE) { level = 0.0f; stage = SYNTH_IDLE; }
                break;
            }
            vv += dvol;
            buf[i] *= level * vv;
            if (stage == SYNTH_IDLE) {
                i++;
                break;
            }
        }
        for (; i < n; i++)
            buf[i] = 0;
        v->level = level;
        v->stage = stage;
        v->inc = inc;
        v->vol = vol;
        v->fresh = 0;
    }

    /* into the mix: the place, the room, the echo, in one pass */
    int pan = (int8_t)r[SYNTH_PAN];
    float gl = pan > 0 ? 1.0f - pan / 127.0f : 1.0f, gr = pan < 0 ? 1.0f + pan / 127.0f : 1.0f;
    float *ml = s->mix[0], *mr = s->mix[1], *mc = s->mix_c;
    uint8_t rs = s->retro ? 0 : r[SYNTH_REVERB], es = s->retro ? 0 : r[SYNTH_ECHO];
    float kr = rs / 255.0f, ke = es / 255.0f;
    float *sr = s->send_room, *se = s->send_echo;
    s->center_used |= !pan;
    if (!pan && rs && !es) {            /* the usual voice: in the middle, a little room */
        for (unsigned i = 0; i < n; i++) {
            mc[i] += buf[i];
            sr[i] += buf[i] * kr;
        }
    } else if (!pan && !rs && !es) {
        for (unsigned i = 0; i < n; i++)
            mc[i] += buf[i];
    } else {
        for (unsigned i = 0; i < n; i++) {
            float x = buf[i];
            ml[i] += x * gl;
            mr[i] += x * gr;
            sr[i] += x * kr;
            se[i] += x * ke;
        }
    }
    if (rs)
        s->room_quiet = 0;
    if (es)
        s->echo_quiet = 0;
}

/* ---------------------------------------------------------------- effects */

/* The room: two diffusers, then eight delay lines that feed each other
 * through a Householder matrix (a feedback delay network), each with its
 * damping; the even lines to the left, the odd ones to the right. It runs
 * at half the rate (a pair of samples in, a straight line between its
 * outputs): a reverb's tail has little above 10 kHz, and it is the
 * dearest part of the sound. */
static void room_render(synth_t *s, unsigned n)
{
    if (s->room_quiet > s->rate / 10u || s->room_wet <= 0.0f)
        return;                         /* nothing in it for a while: no work */
    float wet = s->room_wet * ROOM_OUT;
    float damp = s->room_damp * 0.6f;
    float energy = 0;
    float pl = s->room_out[0], pr = s->room_out[1];
    for (unsigned i = 0; i + 1 < n; i += 2) {
        float x = (s->send_room[i] + s->send_room[i + 1]) * 0.5f;
        for (int k = 0; k < 2; k++) {
            float *ap = s->room_ap[k];
            uint16_t j = s->room_api[k];
            float b = ap[j];
            float y = b - ROOM_AP_G * x;
            ap[j] = x + ROOM_AP_G * y;
            if (++j >= room_ap_len[k])
                j = 0;
            s->room_api[k] = j;
            x = y;
        }
        float o[SYNTH_ROOM_LINES], sum = 0;
        for (int l = 0; l < SYNTH_ROOM_LINES; l++) {
            float y = s->room[l][s->room_i[l]];
            s->room_lp[l] = y + (s->room_lp[l] - y) * damp;
            o[l] = s->room_lp[l] * s->room_g[l];
            sum += o[l];
        }
        sum *= 2.0f / SYNTH_ROOM_LINES;
        float left = 0, right = 0;
        for (int l = 0; l < SYNTH_ROOM_LINES; l++) {
            s->room[l][s->room_i[l]] = o[l] - sum + x;
            if (++s->room_i[l] >= s->room_len[l])
                s->room_i[l] = 0;
            if (l & 1)
                right += o[l];
            else
                left += o[l];
        }
        left *= wet;
        right *= wet;
        s->mix[0][i] += (pl + left) * 0.5f;
        s->mix[1][i] += (pr + right) * 0.5f;
        s->mix[0][i + 1] += left;
        s->mix[1][i + 1] += right;
        pl = left;
        pr = right;
        energy += (left < 0 ? -left : left) + (right < 0 ? -right : right);
    }
    s->room_out[0] = pl;
    s->room_out[1] = pr;
    if (energy < QUIET * (float)n)
        s->room_quiet += n;
}

/* The echo: left then right, each repeat a little duller (ping-pong). */
static void echo_render(synth_t *s, unsigned n)
{
    if (s->echo_quiet > s->rate / 10u || s->echo_wet <= 0.0f)
        return;
    float fb = s->echo_fb, wet = s->echo_wet * 0.8f;
    uint32_t len = s->echo_len, w = s->echo_i;
    float lp0 = s->echo_lp[0], lp1 = s->echo_lp[1];
    float energy = 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t r = (w - len) & (SYNTH_ECHO_LEN - 1);
        float l = s->echo[0][r], rr = s->echo[1][r];
        lp0 += 0.55f * (l - lp0);
        lp1 += 0.55f * (rr - lp1);
        float in = s->send_echo[i];
        float a = in + fb * lp1, b = fb * lp0;
        s->echo[0][w] = a;
        s->echo[1][w] = b;
        w = (w + 1) & (SYNTH_ECHO_LEN - 1);
        s->mix[0][i] += l * wet;
        s->mix[1][i] += rr * wet;
        energy += (l < 0 ? -l : l) + (rr < 0 ? -rr : rr) + (in < 0 ? -in : in);
    }
    s->echo_i = w;
    s->echo_lp[0] = lp0;
    s->echo_lp[1] = lp1;
    if (energy < QUIET * (float)n)
        s->echo_quiet += n;
}

void synth_render(synth_t *s, const volatile uint8_t *regs, int16_t *out, unsigned n)
{
    while (n) {
        unsigned m = n < SYNTH_BLOCK ? n : SYNTH_BLOCK;
        for (unsigned i = 0; i < m; i++) {
            s->mix[0][i] = s->mix[1][i] = s->mix_c[i] = 0;
            s->send_room[i] = s->send_echo[i] = 0;
        }
        s->center_used = 0;
        for (unsigned ch = 0; ch < SYNTH_VOICES; ch++)
            render_voice(s, ch, regs + ch * SYNTH_VOICE_BYTES, m);
        if (s->center_used)
            for (unsigned i = 0; i < m; i++) {
                s->mix[0][i] += s->mix_c[i];
                s->mix[1][i] += s->mix_c[i];
            }
        float g = s->gain;
        if (!synth_active(s) && s->room_quiet > s->rate / 10u && s->echo_quiet > s->rate / 10u &&
            s->dc_y[0] == 0 && s->dc_y[1] == 0) {
            for (unsigned i = 0; i < 2 * m; i++)
                out[i] = 0;             /* all quiet: nothing to mix */
            s->comp_env = 0;
            s->comp_gain = COMP_MAKEUP;
            s->dc_x[0] = s->dc_x[1] = 0;
            out += 2 * m;
            n -= m;
            continue;
        }
        if (s->retro) {
            for (unsigned i = 0; i < m; i++) {
                out[2 * i] = (int16_t)(synth_limit(s->mix[0][i] * g) * 32767.0f);
                out[2 * i + 1] = (int16_t)(synth_limit(s->mix[1][i] * g) * 32767.0f);
            }
        } else {
            room_render(s, m);
            echo_render(s, m);
            float peak = 0;
            for (int c = 0; c < 2; c++) {
                float x1 = s->dc_x[c], y1 = s->dc_y[c];
                float *x = s->mix[c];
                for (unsigned i = 0; i < m; i++) {
                    /* the DC blocker (about 4 Hz): a square of any duty swings around 0 */
                    float y = x[i] - x1 + 0.9995f * y1;
                    x1 = x[i];
                    y1 = (y < 1e-7f && y > -1e-7f && x[i] == 0) ? 0 : y;
                    x[i] = y;
                    float a = y1 < 0 ? -y1 : y1;
                    if (a > peak)
                        peak = a;
                }
                s->dc_x[c] = x1;
                s->dc_y[c] = y1;
            }
            /* the compressor: past COMP_T the level grows a third as fast,
             * so many loud voices together are turned down smoothly before
             * the limiter has to bend them (which is heard as distortion) */
            if (peak > s->comp_env)
                s->comp_env = peak;
            else
                s->comp_env += (peak - s->comp_env) * COMP_RELEASE;
            float want = s->comp_env > COMP_T ? powf(COMP_T / s->comp_env, 0.6667f) : 1.0f;    /* once a block */
            if (s->comp_env * want > COMP_TOP)
                want = COMP_TOP / s->comp_env;      /* far over: a ceiling, not a slope */
            want *= COMP_MAKEUP;
            /* the master volume last, like the knob of the headphones: the
             * compressor hears the game's own mix whatever the volume */
            float k = s->comp_gain, dk = (want - k) / (float)m, out_g = g * 32767.0f;
            for (unsigned i = 0; i < m; i++) {
                k += dk;
                out[2 * i] = (int16_t)(synth_limit(s->mix[0][i] * k) * out_g);
                out[2 * i + 1] = (int16_t)(synth_limit(s->mix[1][i] * k) * out_g);
            }
            s->comp_gain = want;
        }
        out += 2 * m;
        n -= m;
    }
}
