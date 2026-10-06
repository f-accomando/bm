#include "presets.h"

#include <math.h>
#include <string.h>

const char *const au_wave_names[SYNTH_WAVES] = {
    "square", "triangle", "saw", "noise", "sine", "metal", "fm", "pluck", "supersaw", "organ"
};

/* register values: times 0..2 s in 255 steps (100 ms = 13), cutoff
 * 20 Hz..20 kHz in 254 steps of half a semitone (synth_cutoff_hz):
 * 200 Hz = 86, 500 = 119, 1 kHz = 145, 2 kHz = 170, 4 kHz = 196, 8 kHz = 221 */
#define T(reg) [SYNTH_##reg - SYNTH_CUTOFF]
#define P(nm, kind, about, w, du, v, a, d, s, r, pi, pt, vd, vr, ...) \
    { { nm, w, du, v, a, d, s, r, pi, pt, vd, vr, 0, { __VA_ARGS__ } }, kind, about }

const au_preset_t au_presets[] = {
    /* drums */
    P("kick", "drum", "808 kick: a sine falling two octaves and a half",
      SYNTH_SINE, 128, 230, 0, 70, 0, 30, 30, 5, 0, 0, T(REVERB) = 10),
    P("punch", "drum", "a short, hard kick with a click",
      SYNTH_SINE, 128, 240, 0, 40, 0, 20, 36, 3, 0, 0, T(NOISEMIX) = 40, T(CUTOFF) = 170, T(DRIVE) = 60,
      T(REVERB) = 10),
    P("snare", "drum", "snare: a tone and a burst of noise",
      SYNTH_TRIANGLE, 128, 170, 0, 24, 0, 22, 10, 3, 0, 0, T(NOISEMIX) = 210, T(CUTOFF) = 211,
      T(RESONANCE) = 30, T(REVERB) = 80),
    P("clap", "drum", "hand clap: noise through a band pass",
      SYNTH_NOISE, 128, 170, 0, 20, 0, 30, 0, 0, 0, 0, T(CUTOFF) = 160, T(RESONANCE) = 60,
      T(FILTER) = SYNTH_BANDPASS, T(REVERB) = 90),
    P("hat", "drum", "closed hi-hat",
      SYNTH_NOISE, 128, 110, 0, 7, 0, 6, 0, 0, 0, 0, T(CUTOFF) = 221, T(FILTER) = SYNTH_HIGHPASS,
      T(REVERB) = 30),
    P("openhat", "drum", "open hi-hat",
      SYNTH_NOISE, 128, 90, 0, 45, 0, 35, 0, 0, 0, 0, T(CUTOFF) = 216, T(FILTER) = SYNTH_HIGHPASS,
      T(REVERB) = 50),
    P("tom", "drum", "tom: a sine falling an octave",
      SYNTH_SINE, 128, 210, 0, 50, 0, 30, 12, 8, 0, 0, T(NOISEMIX) = 30, T(REVERB) = 70),
    P("rim", "drum", "rim shot",
      SYNTH_TRIANGLE, 128, 150, 0, 5, 0, 5, 0, 0, 0, 0, T(NOISEMIX) = 80, T(CUTOFF) = 170, T(RESONANCE) = 80,
      T(FILTER) = SYNTH_BANDPASS, T(REVERB) = 60),
    P("crash", "drum", "crash cymbal",
      SYNTH_NOISE, 128, 110, 0, 200, 0, 180, 0, 0, 0, 0, T(CUTOFF) = 204, T(FILTER) = SYNTH_HIGHPASS,
      T(REVERB) = 120),
    P("cowbell", "drum", "cowbell: a square through a ringing band pass",
      SYNTH_SQUARE, 128, 120, 0, 25, 0, 20, 0, 0, 0, 0, T(CUTOFF) = 137, T(RESONANCE) = 120,
      T(FILTER) = SYNTH_BANDPASS | SYNTH_KEYTRACK, T(REVERB) = 50),
    P("shaker", "drum", "shaker",
      SYNTH_NOISE, 128, 80, 1, 12, 0, 10, 0, 0, 0, 0, T(CUTOFF) = 211, T(RESONANCE) = 40,
      T(FILTER) = SYNTH_BANDPASS, T(REVERB) = 40),
    /* basses */
    P("bass", "bass", "round bass: a saw under a low pass that opens on the note",
      SYNTH_SAW, 128, 200, 0, 60, 120, 10, 0, 0, 0, 0, T(CUTOFF) = 95, T(RESONANCE) = 60, T(FENV) = 30,
      T(FDECAY) = 30, T(REVERB) = 10),
    P("acid", "bass", "acid bass: resonant, driven, the filter snapping shut",
      SYNTH_SAW, 128, 170, 0, 50, 140, 8, 0, 0, 0, 0, T(CUTOFF) = 70, T(RESONANCE) = 190, T(FENV) = 56,
      T(FDECAY) = 22, T(DRIVE) = 70),
    P("sub", "bass", "sub bass: a plain sine",
      SYNTH_SINE, 128, 230, 1, 0, 255, 12, 0, 0, 0, 0, T(REVERB) = 0),
    P("fmbass", "bass", "FM bass: bright on the note, then round",
      SYNTH_FM, 128, 190, 0, 60, 100, 10, 0, 0, 0, 0, T(MOD1) = 16, T(MOD2) = 96, T(MODDECAY) = 30,
      T(REVERB) = 10),
    P("pickbass", "bass", "plucked bass string",
      SYNTH_PLUCK, 128, 230, 0, 0, 255, 20, 0, 0, 0, 0, T(MOD1) = 60, T(MOD2) = 150, T(REVERB) = 10),
    /* keys */
    P("epiano", "keys", "electric piano (FM)",
      SYNTH_FM, 128, 130, 0, 150, 70, 60, 0, 0, 0, 0, T(MOD1) = 16, T(MOD2) = 80, T(MODDECAY) = 70,
      T(REVERB) = 80),
    P("organ", "keys", "drawbar organ",
      SYNTH_ORGAN, 128, 140, 3, 0, 255, 25, 0, 0, 0, 0, T(REVERB) = 90),
    P("bell", "keys", "bell (FM, an inharmonic ratio)",
      SYNTH_FM, 128, 140, 0, 160, 0, 160, 0, 0, 0, 0, T(MOD1) = 56, T(MOD2) = 110, T(MODDECAY) = 90,
      T(REVERB) = 140, T(ECHO) = 40),
    P("marimba", "keys", "marimba (FM, short)",
      SYNTH_FM, 128, 170, 0, 45, 0, 40, 0, 0, 0, 0, T(MOD1) = 64, T(MOD2) = 60, T(MODDECAY) = 15,
      T(REVERB) = 70),
    P("glock", "keys", "glockenspiel",
      SYNTH_FM, 128, 120, 0, 100, 0, 100, 0, 0, 0, 0, T(MOD1) = 112, T(MOD2) = 50, T(MODDECAY) = 40,
      T(REVERB) = 120),
    /* pads */
    P("pad", "pad", "wide pad: three saws, a breathing filter",
      SYNTH_SUPERSAW, 128, 90, 90, 60, 220, 110, 0, 0, 0, 0, T(CUTOFF) = 150, T(RESONANCE) = 50,
      T(LFO_RATE) = 30, T(LFO_CUT) = 50, T(REVERB) = 150),
    P("strings", "pad", "string section",
      SYNTH_SUPERSAW, 128, 110, 70, 0, 255, 90, 0, 0, 8, 50, T(MOD1) = 40, T(CUTOFF) = 185,
      T(RESONANCE) = 20, T(REVERB) = 140),
    P("warm", "pad", "warm pad: a slow triangle",
      SYNTH_TRIANGLE, 128, 140, 80, 0, 255, 100, 0, 0, 6, 45, T(REVERB) = 150),
    P("glass", "pad", "glassy pad (FM)",
      SYNTH_FM, 128, 110, 100, 0, 255, 120, 0, 0, 0, 0, T(MOD1) = 32, T(MOD2) = 30, T(REVERB) = 160),
    /* plucks */
    P("pluck", "pluck", "plucked string, bright, with an echo",
      SYNTH_PLUCK, 128, 170, 0, 0, 255, 50, 0, 0, 0, 0, T(MOD1) = 150, T(MOD2) = 150, T(REVERB) = 60,
      T(ECHO) = 60),
    P("guitar", "pluck", "nylon guitar",
      SYNTH_PLUCK, 128, 180, 0, 0, 255, 40, 0, 0, 0, 0, T(MOD1) = 90, T(MOD2) = 190, T(REVERB) = 50),
    P("harp", "pluck", "harp",
      SYNTH_PLUCK, 128, 150, 0, 0, 255, 100, 0, 0, 0, 0, T(MOD1) = 220, T(MOD2) = 210, T(REVERB) = 120),
    /* leads */
    P("lead", "lead", "soft lead: a square whose width moves",
      SYNTH_SQUARE, 128, 110, 3, 50, 190, 40, 0, 0, 18, 55, T(CUTOFF) = 200, T(RESONANCE) = 70,
      T(LFO_RATE) = 60, T(LFO_PWM) = 110, T(REVERB) = 80, T(ECHO) = 60),
    P("sawlead", "lead", "big lead: three saws",
      SYNTH_SUPERSAW, 128, 100, 2, 40, 200, 30, 0, 0, 12, 55, T(CUTOFF) = 210, T(RESONANCE) = 40,
      T(REVERB) = 60, T(ECHO) = 40),
    P("flute", "lead", "flute: a soft tone and its breath",
      SYNTH_ORGAN, 128, 140, 12, 0, 255, 25, 0, 0, 15, 50, T(MOD1) = 0xF3, T(MOD2) = 0x10,
      T(NOISEMIX) = 25, T(CUTOFF) = 196, T(REVERB) = 100),
    P("brass", "lead", "brass: the filter opens as it swells",
      SYNTH_SAW, 128, 150, 10, 60, 180, 30, 0, 0, 8, 50, T(CUTOFF) = 110, T(RESONANCE) = 30, T(FENV) = 40,
      T(FDECAY) = 50, T(REVERB) = 70),
    P("chip", "lead", "the 8-bit square of the first versions",
      SYNTH_SQUARE, 128, 120, 1, 0, 255, 10, 0, 0, 0, 0, T(FLAGS) = SYNTH_FLAG_RAW),
    P("chiptri", "lead", "the 8-bit triangle",
      SYNTH_TRIANGLE, 128, 160, 1, 0, 255, 10, 0, 0, 0, 0, T(FLAGS) = SYNTH_FLAG_RAW),
    P("chipkick", "drum", "the 8-bit kick: a triangle falling fast",
      SYNTH_TRIANGLE, 128, 230, 0, 30, 0, 20, 24, 4, 0, 0, T(FLAGS) = SYNTH_FLAG_RAW),
    P("chipsnr", "drum", "the 8-bit snare: noise",
      SYNTH_NOISE, 128, 170, 0, 18, 0, 14, 0, 0, 0, 0, T(FLAGS) = SYNTH_FLAG_RAW),
    P("chiphat", "drum", "the 8-bit hi-hat: metal noise",
      SYNTH_METAL, 128, 90, 0, 5, 0, 4, 0, 0, 0, 0, T(FLAGS) = SYNTH_FLAG_RAW),
    /* effects */
    P("laser", "fx", "laser: falling two octaves",
      SYNTH_SQUARE, 128, 140, 0, 20, 0, 10, 24, 20, 0, 0, T(CUTOFF) = 211, T(REVERB) = 40),
    P("blip", "fx", "menu blip",
      SYNTH_SQUARE, 64, 120, 0, 12, 0, 10, 0, 0, 0, 0, T(CUTOFF) = 211, T(REVERB) = 30),
    P("boom", "fx", "explosion: noise, a closing filter, drive",
      SYNTH_NOISE, 128, 230, 0, 150, 0, 120, 0, 0, 0, 0, T(CUTOFF) = 110, T(FENV) = 40, T(FDECAY) = 60,
      T(DRIVE) = 80, T(REVERB) = 100),
    P("wind", "fx", "wind: noise through a slowly moving band",
      SYNTH_NOISE, 128, 90, 150, 0, 255, 150, 0, 0, 0, 0, T(CUTOFF) = 137, T(RESONANCE) = 120,
      T(FILTER) = SYNTH_BANDPASS, T(LFO_RATE) = 20, T(LFO_CUT) = 100, T(REVERB) = 120),
};
const int au_preset_count = sizeof au_presets / sizeof au_presets[0];

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

int au_preset_find(const char *name)
{
    for (int i = 0; i < au_preset_count; i++)
        if (same(name, au_presets[i].s.name))
            return i;
    return -1;
}

int au_wave_find(const char *name)
{
    if (name[0] >= '0' && name[0] <= '9') {
        int w = name[0] - '0';
        return !name[1] && w < SYNTH_WAVES ? w : -1;
    }
    for (int i = 0; i < SYNTH_WAVES; i++)
        if (same(name, au_wave_names[i]))
            return i;
    return -1;
}

void au_sound_regs(const au_sound_t *s, volatile uint8_t *r)
{
    r[SYNTH_WAVEFORM] = s->wave;
    r[SYNTH_DUTY] = s->duty;
    r[SYNTH_VOLUME] = s->vol;
    r[SYNTH_ATTACK] = s->attack;
    r[SYNTH_DECAY] = s->decay;
    r[SYNTH_SUSTAIN] = s->sustain;
    r[SYNTH_RELEASE] = s->release;
    for (int i = 0; i < AU_TONE; i++)
        r[SYNTH_CUTOFF + i] = s->tone[i];
}

/* ---------------------------------------------------------------- plain units */

static uint8_t unit8(double v)
{
    double x = v * 255.0 + 0.5;
    return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

static uint8_t ms8(double ms)
{
    double x = ms * (255.0 / 2000.0) + 0.5;
    return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

static uint8_t s8(double v)
{
    long x = lround(v);
    return (uint8_t)(int8_t)(x < -127 ? -127 : x > 127 ? 127 : x);
}

static uint8_t cutoff8(double hz)
{
    if (hz <= 0)
        return 0;
    double x = 1.0 + log2(hz / 20.0) * (254.0 / 9.9658) + 0.5;
    return (uint8_t)(x < 1 ? 1 : x > 255 ? 255 : x);
}

static uint8_t bar(double v)
{
    long x = lround(v);
    return (uint8_t)(x < 0 ? 0 : x > 15 ? 15 : x);
}

int au_tone_num(volatile uint8_t *r, const char *k, double v)
{
    if (same(k, "wave"))            r[SYNTH_WAVEFORM] = (uint8_t)(v >= 0 && v < SYNTH_WAVES ? v : 0);
    else if (same(k, "vol"))        r[SYNTH_VOLUME] = unit8(v);
    else if (same(k, "duty"))       r[SYNTH_DUTY] = unit8(v);
    else if (same(k, "attack"))     r[SYNTH_ATTACK] = ms8(v);
    else if (same(k, "decay"))      r[SYNTH_DECAY] = ms8(v);
    else if (same(k, "sustain"))    r[SYNTH_SUSTAIN] = unit8(v);
    else if (same(k, "release"))    r[SYNTH_RELEASE] = ms8(v);
    else if (same(k, "cutoff"))     r[SYNTH_CUTOFF] = cutoff8(v);
    else if (same(k, "res"))        r[SYNTH_RESONANCE] = unit8(v);
    else if (same(k, "keytrack"))   r[SYNTH_FILTER] = (uint8_t)((r[SYNTH_FILTER] & 3) | (v != 0 ? SYNTH_KEYTRACK : 0));
    else if (same(k, "fenv"))       r[SYNTH_FENV] = s8(v * 16.0);
    else if (same(k, "fdecay"))     r[SYNTH_FDECAY] = ms8(v);
    else if (same(k, "pan"))        r[SYNTH_PAN] = s8(v * 127.0);
    else if (same(k, "noise"))      r[SYNTH_NOISEMIX] = unit8(v);
    else if (same(k, "drive"))      r[SYNTH_DRIVE] = unit8(v);
    else if (same(k, "reverb"))     r[SYNTH_REVERB] = unit8(v);
    else if (same(k, "echo"))       r[SYNTH_ECHO] = unit8(v);
    else if (same(k, "lfo")) {
        /* 0.1 .. 20 Hz on 1..255, as synth.c's lfo_hz */
        double x = v <= 0 ? 0 : 1.0 + log2(v / 0.1) * (254.0 / 7.64) + 0.5;
        r[SYNTH_LFO_RATE] = (uint8_t)(v <= 0 ? 0 : x < 1 ? 1 : x > 255 ? 255 : x);
    }
    else if (same(k, "wah"))        r[SYNTH_LFO_CUT] = unit8(v / 4.0);
    else if (same(k, "pwm"))        r[SYNTH_LFO_PWM] = unit8(v);
    else if (same(k, "ratio"))      r[SYNTH_MOD1] = (uint8_t)(v * 16.0 + 0.5 < 1 ? 1 : v * 16.0 + 0.5 > 255 ? 255 : v * 16.0 + 0.5);
    else if (same(k, "depth"))      r[SYNTH_MOD2] = unit8(v / 8.0 * 256.0 / 255.0);
    else if (same(k, "mdecay"))     r[SYNTH_MODDECAY] = ms8(v);
    else if (same(k, "feedback"))   r[SYNTH_FMFB] = unit8(v);
    else if (same(k, "bright"))     r[SYNTH_MOD1] = unit8(v) ? unit8(v) : 1;
    else if (same(k, "ring"))       r[SYNTH_MOD2] = unit8(v) ? unit8(v) : 1;
    else if (same(k, "spread"))     r[SYNTH_MOD1] = unit8(v) ? unit8(v) : 1;
    else if (same(k, "bar1"))       r[SYNTH_MOD1] = (uint8_t)((r[SYNTH_MOD1] & 0x0F) | bar(v) << 4);
    else if (same(k, "bar2"))       r[SYNTH_MOD1] = (uint8_t)((r[SYNTH_MOD1] & 0xF0) | bar(v));
    else if (same(k, "bar3"))       r[SYNTH_MOD2] = (uint8_t)((r[SYNTH_MOD2] & 0x0F) | bar(v) << 4);
    else if (same(k, "bar4"))       r[SYNTH_MOD2] = (uint8_t)((r[SYNTH_MOD2] & 0xF0) | bar(v));
    else if (same(k, "raw"))        r[SYNTH_FLAGS] = (uint8_t)((r[SYNTH_FLAGS] & ~SYNTH_FLAG_RAW) | (v != 0 ? SYNTH_FLAG_RAW : 0));
    else
        return -1;
    return 0;
}

int au_tone_str(volatile uint8_t *r, const char *k, const char *v)
{
    if (same(k, "wave")) {
        int w = au_wave_find(v);
        if (w < 0)
            return -1;
        r[SYNTH_WAVEFORM] = (uint8_t)w;
        return 0;
    }
    if (same(k, "filter")) {
        static const char *const modes[4] = { "lp", "bp", "hp", "notch" };
        for (int m = 0; m < 4; m++)
            if (same(v, modes[m])) {
                r[SYNTH_FILTER] = (uint8_t)((r[SYNTH_FILTER] & SYNTH_KEYTRACK) | m);
                return 0;
            }
        return -1;
    }
    return -1;
}
