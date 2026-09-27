#include "synth.h"

#define MAX_ENVELOPE_SECONDS 2.0f

void synth_init(synth_t *s, uint32_t rate)
{
    s->rate = rate;
    for (unsigned i = 0; i < SYNTH_VOICES; i++) {
        synth_voice_t *v = &s->v[i];
        v->phase = 0;
        v->level = 0;
        v->stage = SYNTH_IDLE;
        v->gated = 0;
        v->lfsr = 1;
        v->noise = -1.0f;
    }
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

/* One voice added into mix[0..n-1]. The registers are constant for the
 * whole call, so everything derived from them is computed once. */
static void render_voice(synth_t *s, synth_voice_t *v, const volatile uint8_t *r,
                         float *mix, unsigned n)
{
    int gate = r[SYNTH_CONTROL] & SYNTH_GATE;

    if (gate && !v->gated) {
        v->stage = SYNTH_ATTACK_ST;
        v->level = 0;
    } else if (!gate && v->gated && v->stage != SYNTH_IDLE) {
        v->stage = SYNTH_RELEASE_ST;
    }
    v->gated = (uint8_t)gate;
    if (v->stage == SYNTH_IDLE)
        return;                 /* silent: no oscillator work at all */

    uint32_t freq = r[SYNTH_FREQ_LO] | (uint32_t)r[SYNTH_FREQ_HI] << 8;
    uint8_t wave = r[SYNTH_WAVEFORM];
    uint32_t duty = r[SYNTH_DUTY] * 0x01010101u;          /* duty/255 of a cycle */
    float vol = r[SYNTH_VOLUME] / 255.0f * (1.0f / 255.0f);    /* level is 0..255 */
    float atk = synth_rate_increment(r[SYNTH_ATTACK], s->rate);
    float dec = synth_rate_increment(r[SYNTH_DECAY], s->rate);
    float sus = r[SYNTH_SUSTAIN];
    float rel = synth_rate_increment(r[SYNTH_RELEASE], s->rate);
    uint32_t inc = (uint32_t)(((uint64_t)freq << 32) / s->rate);
    int fast_noise = freq >= s->rate;           /* at least one step per sample */
    uint32_t phase = v->phase;
    float level = v->level;
    uint8_t stage = v->stage;

    for (unsigned i = 0; i < n; i++) {
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

        float value;
        uint32_t prev = phase;
        phase += inc;
        if (wave == SYNTH_NOISE) {
            if (fast_noise || phase < prev) {
                unsigned b0 = v->lfsr & 1, b1 = v->lfsr >> 1 & 1;
                v->lfsr = (uint16_t)(v->lfsr >> 1 | (b0 ^ b1) << 14);
                v->noise = b0 ? 1.0f : -1.0f;
            }
            value = v->noise;
        } else if (wave == SYNTH_SQUARE) {
            value = phase < duty ? 1.0f : -1.0f;
        } else {
            float p = (float)phase * (1.0f / 4294967296.0f);
            if (wave == SYNTH_TRIANGLE)
                value = p < 0.5f ? -1.0f + 4.0f * p : 3.0f - 4.0f * p;
            else
                value = 2.0f * p - 1.0f;
        }
        mix[i] += value * level * vol;
        if (stage == SYNTH_IDLE)
            break;
    }
    v->phase = phase;
    v->level = level;
    v->stage = stage;
}

#define BLOCK 128

void synth_render(synth_t *s, const volatile uint8_t *regs, int16_t *out, unsigned n)
{
    /* bigger calls are cut into blocks only to bound the mix buffer on
     * the stack (the registers cannot change during a call) */
    while (n) {
        unsigned m = n < BLOCK ? n : BLOCK;
        float mix[BLOCK];
        for (unsigned i = 0; i < m; i++)
            mix[i] = 0;
        for (unsigned ch = 0; ch < SYNTH_VOICES; ch++)
            render_voice(s, &s->v[ch], regs + ch * SYNTH_VOICE_BYTES, mix, m);
        for (unsigned i = 0; i < m; i++) {
            float x = mix[i];
            if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
            out[i] = (int16_t)(x * 32767.0f);
        }
        out += m;
        n -= m;
    }
}
